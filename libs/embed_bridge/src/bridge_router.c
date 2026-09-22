#include "embed_bridge/bridge_router.h"
#include <stdio.h>
#include <string.h>

void bridge_router_init(bridge_router_t *router) {
    if (!router)
        return;
    memset(router, 0, sizeof(bridge_router_t));
}

void bridge_router_bind_modbus(bridge_router_t *router, uint16_t *holding_regs, size_t reg_count) {
    if (!router)
        return;
    router->modbus_holding_regs = holding_regs;
    router->modbus_reg_count = reg_count;
}

void bridge_router_bind_opcua(bridge_router_t *router, opcua_server_t *opcua_server) {
    if (!router)
        return;
    router->opcua_server = opcua_server;
}

void bridge_router_bind_eip(bridge_router_t *router, eip_adapter_t *eip_adapter) {
    if (!router)
        return;
    router->eip_adapter = eip_adapter;
}

void bridge_router_bind_canopen(bridge_router_t *router, canopen_node_t *canopen_node) {
    if (!router)
        return;
    router->canopen_node = canopen_node;
}

void bridge_router_bind_mqtt(bridge_router_t *router, mqtt_client_t *mqtt_client) {
    if (!router)
        return;
    router->mqtt_client = mqtt_client;
}

static size_t get_type_size(bridge_data_type_t t) {
    switch (t) {
    case BRIDGE_TYPE_BOOL:
        return 1;
    case BRIDGE_TYPE_INT16:
    case BRIDGE_TYPE_UINT16:
        return 2;
    case BRIDGE_TYPE_INT32:
    case BRIDGE_TYPE_UINT32:
    case BRIDGE_TYPE_FLOAT:
        return 4;
    default:
        return 2;
    }
}

int bridge_router_add_rule(bridge_router_t *router, const bridge_rule_t *rule) {
    if (!router || !rule)
        return -1;
    if (router->rule_count >= BRIDGE_MAX_RULES)
        return -1;

    size_t idx = router->rule_count;
    router->rules[idx] = *rule;
    router->rules[idx].rule_id = (uint16_t)(idx + 1);
    router->rules[idx].is_active = true;
    router->rules[idx].has_last_data = false;
    router->rules[idx].timer_ms = 0;

    if (router->rules[idx].data_size == 0) {
        router->rules[idx].data_size = get_type_size(rule->src_type);
    }

    router->rule_count++;
    return (int)idx;
}

static bool read_endpoint(const bridge_router_t *router, const bridge_endpoint_t *ep,
                          bridge_data_type_t src_type, size_t sz, uint8_t *buf, float *out_val) {
    if (!router || !ep || !buf || !out_val)
        return false;

    switch (ep->proto) {
    case BRIDGE_PROTO_RAW_VAR: {
        if (!ep->target.raw_ptr)
            return false;
        memcpy(buf, ep->target.raw_ptr, sz);
        break;
    }

    case BRIDGE_PROTO_MODBUS_REG: {
        uint16_t addr = ep->target.modbus_addr;
        if (!router->modbus_holding_regs || addr >= router->modbus_reg_count)
            return false;

        if (sz == 2) {
            uint16_t r = router->modbus_holding_regs[addr];
            memcpy(buf, &r, 2);
        } else if (sz == 4) {
            if ((size_t)addr + 1 >= router->modbus_reg_count)
                return false;
            uint32_t val32 = ((uint32_t)router->modbus_holding_regs[addr] << 16) |
                             router->modbus_holding_regs[addr + 1];
            memcpy(buf, &val32, 4);
        } else {
            return false;
        }
        break;
    }

    case BRIDGE_PROTO_OPCUA_NODE: {
        if (!router->opcua_server)
            return false;
        opcua_datavalue_t dv;
        if (!opcua_server_read_value(router->opcua_server, &ep->target.opcua_node, &dv))
            return false;

        switch (dv.value.type) {
        case OPCUA_TYPE_BOOLEAN: {
            buf[0] = dv.value.value.boolean ? 1 : 0;
            *out_val = (float)buf[0];
            return true;
        }
        case OPCUA_TYPE_INT16: {
            memcpy(buf, &dv.value.value.int16_val, 2);
            *out_val = (float)dv.value.value.int16_val;
            return true;
        }
        case OPCUA_TYPE_UINT16: {
            memcpy(buf, &dv.value.value.uint16_val, 2);
            *out_val = (float)dv.value.value.uint16_val;
            return true;
        }
        case OPCUA_TYPE_INT32: {
            memcpy(buf, &dv.value.value.int32_val, 4);
            *out_val = (float)dv.value.value.int32_val;
            return true;
        }
        case OPCUA_TYPE_UINT32: {
            memcpy(buf, &dv.value.value.uint32_val, 4);
            *out_val = (float)dv.value.value.uint32_val;
            return true;
        }
        case OPCUA_TYPE_FLOAT: {
            memcpy(buf, &dv.value.value.float_val, 4);
            *out_val = dv.value.value.float_val;
            return true;
        }
        case OPCUA_TYPE_DOUBLE: {
            float f = (float)dv.value.value.double_val;
            memcpy(buf, &f, 4);
            *out_val = f;
            return true;
        }
        default:
            return false;
        }
    }

    case BRIDGE_PROTO_EIP_ASSEMBLY: {
        if (!router->eip_adapter)
            return false;
        uint8_t assem[128] = {0};
        size_t assem_sz = 0;
        if (!eip_adapter_get_assembly_data(router->eip_adapter, ep->target.eip.instance_id, assem,
                                           &assem_sz)) {
            return false;
        }
        if (ep->target.eip.offset + sz > assem_sz)
            return false;
        memcpy(buf, assem + ep->target.eip.offset, sz);
        break;
    }

    case BRIDGE_PROTO_CANOPEN_OD: {
        if (!router->canopen_node)
            return false;
        size_t out_len = 0;
        if (!canopen_od_read(&router->canopen_node->od, ep->target.canopen.index,
                             ep->target.canopen.subindex, buf, sz, &out_len)) {
            return false;
        }
        break;
    }

    default:
        return false;
    }

    // 数値の抽出
    switch (src_type) {
    case BRIDGE_TYPE_BOOL: {
        *out_val = (float)(buf[0]);
        break;
    }
    case BRIDGE_TYPE_INT16: {
        int16_t v;
        memcpy(&v, buf, 2);
        *out_val = (float)v;
        break;
    }
    case BRIDGE_TYPE_UINT16: {
        uint16_t v;
        memcpy(&v, buf, 2);
        *out_val = (float)v;
        break;
    }
    case BRIDGE_TYPE_INT32: {
        int32_t v;
        memcpy(&v, buf, 4);
        *out_val = (float)v;
        break;
    }
    case BRIDGE_TYPE_UINT32: {
        uint32_t v;
        memcpy(&v, buf, 4);
        *out_val = (float)v;
        break;
    }
    case BRIDGE_TYPE_FLOAT: {
        float v;
        memcpy(&v, buf, 4);
        *out_val = v;
        break;
    }
    default:
        *out_val = 0.0f;
        break;
    }

    return true;
}

static bool write_endpoint(bridge_router_t *router, const bridge_endpoint_t *ep,
                           bridge_data_type_t dst_type, size_t sz, const uint8_t *buf, float val) {
    if (!router || !ep || !buf)
        return false;

    switch (ep->proto) {
    case BRIDGE_PROTO_RAW_VAR: {
        if (!ep->target.raw_ptr)
            return false;
        memcpy(ep->target.raw_ptr, buf, sz);
        return true;
    }

    case BRIDGE_PROTO_MODBUS_REG: {
        uint16_t addr = ep->target.modbus_addr;
        if (!router->modbus_holding_regs || addr >= router->modbus_reg_count)
            return false;

        if (sz == 2) {
            uint16_t r;
            memcpy(&r, buf, 2);
            router->modbus_holding_regs[addr] = r;
        } else if (sz == 4) {
            if ((size_t)addr + 1 >= router->modbus_reg_count)
                return false;
            uint32_t val32;
            memcpy(&val32, buf, 4);
            router->modbus_holding_regs[addr] = (uint16_t)(val32 >> 16);
            router->modbus_holding_regs[addr + 1] = (uint16_t)(val32 & 0xFFFF);
        }
        return true;
    }

    case BRIDGE_PROTO_OPCUA_NODE: {
        if (!router->opcua_server)
            return false;
        opcua_variant_t var;
        memset(&var, 0, sizeof(var));

        switch (dst_type) {
        case BRIDGE_TYPE_BOOL:
            var.type = OPCUA_TYPE_BOOLEAN;
            var.value.boolean = (val != 0.0f);
            break;
        case BRIDGE_TYPE_INT16:
            var.type = OPCUA_TYPE_INT16;
            var.value.int16_val = (int16_t)val;
            break;
        case BRIDGE_TYPE_UINT16:
            var.type = OPCUA_TYPE_UINT16;
            var.value.uint16_val = (uint16_t)val;
            break;
        case BRIDGE_TYPE_INT32:
            var.type = OPCUA_TYPE_INT32;
            var.value.int32_val = (int32_t)val;
            break;
        case BRIDGE_TYPE_UINT32:
            var.type = OPCUA_TYPE_UINT32;
            var.value.uint32_val = (uint32_t)val;
            break;
        case BRIDGE_TYPE_FLOAT:
            var.type = OPCUA_TYPE_FLOAT;
            var.value.float_val = val;
            break;
        default:
            return false;
        }
        return opcua_server_write_value(router->opcua_server, &ep->target.opcua_node, &var);
    }

    case BRIDGE_PROTO_EIP_ASSEMBLY: {
        if (!router->eip_adapter)
            return false;
        uint8_t assem[128] = {0};
        size_t assem_sz = 0;
        if (!eip_adapter_get_assembly_data(router->eip_adapter, ep->target.eip.instance_id, assem,
                                           &assem_sz)) {
            return false;
        }
        if (ep->target.eip.offset + sz > sizeof(assem))
            return false;
        memcpy(assem + ep->target.eip.offset, buf, sz);
        if (ep->target.eip.offset + sz > assem_sz) {
            assem_sz = ep->target.eip.offset + sz;
        }
        return eip_adapter_set_assembly_data(router->eip_adapter, ep->target.eip.instance_id, assem,
                                             assem_sz);
    }

    case BRIDGE_PROTO_CANOPEN_OD: {
        if (!router->canopen_node)
            return false;
        return canopen_od_write(&router->canopen_node->od, ep->target.canopen.index,
                                ep->target.canopen.subindex, buf, sz);
    }

    case BRIDGE_PROTO_MQTT_TOPIC: {
        if (!router->mqtt_client)
            return false;
        char payload[64] = {0};
        if (dst_type == BRIDGE_TYPE_FLOAT) {
            snprintf(payload, sizeof(payload), "%.2f", val);
        } else if (dst_type == BRIDGE_TYPE_INT16 || dst_type == BRIDGE_TYPE_INT32) {
            snprintf(payload, sizeof(payload), "%d", (int32_t)val);
        } else {
            snprintf(payload, sizeof(payload), "%u", (uint32_t)val);
        }
        return mqtt_client_publish(router->mqtt_client, ep->target.mqtt_topic, payload,
                                   strlen(payload), MQTT_QOS_0, false);
    }

    default:
        return false;
    }
}

static bool sync_rule(bridge_router_t *router, bridge_rule_t *rule) {
    if (!router || !rule || !rule->is_active)
        return false;

    uint8_t src_buf[BRIDGE_MAX_DATA_SIZE] = {0};
    float raw_val = 0.0f;

    if (!read_endpoint(router, &rule->src, rule->src_type, rule->data_size, src_buf, &raw_val)) {
        return false;
    }

    // 変化検知 (COV: Change of Value) 判定
    if (rule->sync_mode == BRIDGE_SYNC_ON_CHANGE) {
        if (rule->has_last_data && memcmp(rule->last_data, src_buf, rule->data_size) == 0) {
            return false; // 変化なし
        }
    }

    // スケール変換の適用
    float final_val = raw_val;
    if (rule->scale != 0.0f) {
        final_val = (raw_val * rule->scale) + rule->offset;
    }

    // 再エンコード
    uint8_t dst_buf[BRIDGE_MAX_DATA_SIZE] = {0};
    size_t dst_sz = get_type_size(rule->dst_type);

    switch (rule->dst_type) {
    case BRIDGE_TYPE_BOOL: {
        dst_buf[0] = (final_val != 0.0f) ? 1 : 0;
        break;
    }
    case BRIDGE_TYPE_INT16: {
        int16_t v = (int16_t)final_val;
        memcpy(dst_buf, &v, 2);
        break;
    }
    case BRIDGE_TYPE_UINT16: {
        uint16_t v = (uint16_t)final_val;
        memcpy(dst_buf, &v, 2);
        break;
    }
    case BRIDGE_TYPE_INT32: {
        int32_t v = (int32_t)final_val;
        memcpy(dst_buf, &v, 4);
        break;
    }
    case BRIDGE_TYPE_UINT32: {
        uint32_t v = (uint32_t)final_val;
        memcpy(dst_buf, &v, 4);
        break;
    }
    case BRIDGE_TYPE_FLOAT: {
        float v = final_val;
        memcpy(dst_buf, &v, 4);
        break;
    }
    default:
        memcpy(dst_buf, src_buf, dst_sz);
        break;
    }

    bool ok = write_endpoint(router, &rule->dst, rule->dst_type, dst_sz, dst_buf, final_val);
    if (ok) {
        memcpy(rule->last_data, src_buf, rule->data_size);
        rule->has_last_data = true;
    }
    return ok;
}

size_t bridge_router_sync_all(bridge_router_t *router) {
    if (!router)
        return 0;
    size_t count = 0;
    for (size_t i = 0; i < router->rule_count; ++i) {
        if (sync_rule(router, &router->rules[i])) {
            count++;
        }
    }
    return count;
}

size_t bridge_router_tick(bridge_router_t *router, uint32_t elapsed_ms) {
    if (!router)
        return 0;
    size_t count = 0;

    for (size_t i = 0; i < router->rule_count; ++i) {
        bridge_rule_t *rule = &router->rules[i];
        if (!rule->is_active)
            continue;

        if (rule->sync_mode == BRIDGE_SYNC_PERIODIC) {
            rule->timer_ms += elapsed_ms;
            if (rule->timer_ms >= rule->interval_ms) {
                rule->timer_ms = 0;
                if (sync_rule(router, rule)) {
                    count++;
                }
            }
        } else if (rule->sync_mode == BRIDGE_SYNC_ON_CHANGE ||
                   rule->sync_mode == BRIDGE_SYNC_ALWAYS) {
            if (sync_rule(router, rule)) {
                count++;
            }
        }
    }

    return count;
}

static modbus_exception_t bridge_mb_read(uint16_t addr, uint16_t count, uint16_t *dst, void *user) {
    bridge_router_t *r = (bridge_router_t *)user;
    if (!r->modbus_holding_regs || addr > r->modbus_reg_count || count > r->modbus_reg_count - addr)
        return MODBUS_EX_ILLEGAL_DATA_ADDRESS;
    memcpy(dst, r->modbus_holding_regs + addr, (size_t)count * sizeof(uint16_t));
    return MODBUS_EX_NONE;
}
static modbus_exception_t bridge_mb_write(uint16_t addr, uint16_t count, const uint16_t *src,
                                          void *user) {
    bridge_router_t *r = (bridge_router_t *)user;
    if (!r->modbus_holding_regs || addr > r->modbus_reg_count || count > r->modbus_reg_count - addr)
        return MODBUS_EX_ILLEGAL_DATA_ADDRESS;
    memcpy(r->modbus_holding_regs + addr, src, (size_t)count * sizeof(uint16_t));
    return MODBUS_EX_NONE;
}
bool bridge_router_init_modbus_slave(bridge_router_t *r, modbus_slave_t *s, uint8_t id) {
    if (!r || !s || !id || id > 247 || !r->modbus_holding_regs)
        return false;
    modbus_slave_callbacks_t callbacks = {0};
    callbacks.read_holding_registers = bridge_mb_read;
    callbacks.write_holding_registers = bridge_mb_write;
    modbus_slave_init(s, id, &callbacks, r);
    return true;
}
