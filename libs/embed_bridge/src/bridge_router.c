#include "embed_bridge/bridge_router.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
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

/* double -> 整数のサチュレーション変換 (範囲外・NaN でも未定義動作にしない) */
static double clamp_d(double v, double lo, double hi) {
    if (v != v) return 0.0;
    return v < lo ? lo : (v > hi ? hi : v);
}
static int16_t to_i16(double v) { return (int16_t)clamp_d(v, -32768.0, 32767.0); }
static uint16_t to_u16(double v) { return (uint16_t)clamp_d(v, 0.0, 65535.0); }
static int32_t to_i32(double v) { return (int32_t)clamp_d(v, -2147483648.0, 2147483647.0); }
static uint32_t to_u32(double v) { return (uint32_t)clamp_d(v, 0.0, 4294967295.0); }

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
    if (rule->data_size > BRIDGE_MAX_DATA_SIZE)
        return -1;
    /* 生バイトは数値と相互変換できない。長さも明示する */
    const bool src_raw = rule->src_type == BRIDGE_TYPE_RAW_BYTES;
    const bool dst_raw = rule->dst_type == BRIDGE_TYPE_RAW_BYTES;
    if (src_raw != dst_raw || (src_raw && rule->data_size == 0))
        return -1;
    if (rule->deadband < 0.0f || rule->deadband != rule->deadband)
        return -1;

    size_t idx = router->rule_count;
    router->rules[idx] = *rule;
    router->rules[idx].rule_id = (uint16_t)(idx + 1);
    router->rules[idx].is_active = true;
    router->rules[idx].has_last_data = false;
    router->rules[idx].timer_ms = 0;
    router->rules[idx].last_value = 0.0;
    router->rules[idx].sync_count = 0;
    router->rules[idx].error_count = 0;

    if (router->rules[idx].data_size == 0) {
        router->rules[idx].data_size = get_type_size(rule->src_type);
    }

    router->rule_count++;
    return (int)idx;
}

static bool read_endpoint(const bridge_router_t *router, const bridge_endpoint_t *ep,
                          bridge_data_type_t src_type, size_t sz, uint8_t *buf, double *out_val) {
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

        if (src_type == BRIDGE_TYPE_RAW_BYTES) {
            /* 生バイト: 1 レジスタ = 2 バイト (上位バイトが先: Modbus の並び) */
            const size_t words = (sz + 1) / 2;
            if (sz % 2 != 0 || words > router->modbus_reg_count - addr)
                return false;
            for (size_t i = 0; i < words; ++i) {
                buf[2 * i] = (uint8_t)(router->modbus_holding_regs[addr + i] >> 8);
                buf[2 * i + 1] = (uint8_t)router->modbus_holding_regs[addr + i];
            }
        } else if (sz == 1) {
            buf[0] = router->modbus_holding_regs[addr] != 0 ? 1 : 0;
        } else if (sz == 2) {
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
            *out_val = (double)buf[0];
            return true;
        }
        case OPCUA_TYPE_INT16: {
            memcpy(buf, &dv.value.value.int16_val, 2);
            *out_val = (double)dv.value.value.int16_val;
            return true;
        }
        case OPCUA_TYPE_UINT16: {
            memcpy(buf, &dv.value.value.uint16_val, 2);
            *out_val = (double)dv.value.value.uint16_val;
            return true;
        }
        case OPCUA_TYPE_INT32: {
            memcpy(buf, &dv.value.value.int32_val, 4);
            *out_val = (double)dv.value.value.int32_val;
            return true;
        }
        case OPCUA_TYPE_UINT32: {
            memcpy(buf, &dv.value.value.uint32_val, 4);
            *out_val = (double)dv.value.value.uint32_val;
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
            *out_val = dv.value.value.double_val;
            return true;
        }
        default:
            return false;
        }
    }

    case BRIDGE_PROTO_EIP_ASSEMBLY: {
        if (!router->eip_adapter)
            return false;
        uint8_t assem[EIP_MAX_ASSEMBLY_SIZE] = {0};
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
        /* 型の長さとオブジェクトの長さが一致しなければ読まない (黙って切り詰めない) */
        const canopen_od_entry_t *e = canopen_od_find_entry_const(
            &router->canopen_node->od, ep->target.canopen.index, ep->target.canopen.subindex);
        size_t out_len = 0;
        if (!e || e->data_len != sz ||
            !canopen_od_read(&router->canopen_node->od, ep->target.canopen.index,
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
        *out_val = (double)(buf[0]);
        break;
    }
    case BRIDGE_TYPE_INT16: {
        int16_t v;
        memcpy(&v, buf, 2);
        *out_val = (double)v;
        break;
    }
    case BRIDGE_TYPE_UINT16: {
        uint16_t v;
        memcpy(&v, buf, 2);
        *out_val = (double)v;
        break;
    }
    case BRIDGE_TYPE_INT32: {
        int32_t v;
        memcpy(&v, buf, 4);
        *out_val = (double)v;
        break;
    }
    case BRIDGE_TYPE_UINT32: {
        uint32_t v;
        memcpy(&v, buf, 4);
        *out_val = (double)v;
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
                           bridge_data_type_t dst_type, size_t sz, const uint8_t *buf, double val) {
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

        if (dst_type == BRIDGE_TYPE_RAW_BYTES) {
            const size_t words = (sz + 1) / 2;
            if (sz % 2 != 0 || words > router->modbus_reg_count - addr)
                return false;
            for (size_t i = 0; i < words; ++i)
                router->modbus_holding_regs[addr + i] =
                    (uint16_t)((buf[2 * i] << 8) | buf[2 * i + 1]);
        } else if (sz == 1) {
            /* BOOL は 0 / 1 のレジスタ値 (旧版は何も書かずに成功を返していた) */
            router->modbus_holding_regs[addr] = buf[0] ? 1 : 0;
        } else if (sz == 2) {
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
        } else {
            return false;
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
            var.value.int16_val = to_i16(val);
            break;
        case BRIDGE_TYPE_UINT16:
            var.type = OPCUA_TYPE_UINT16;
            var.value.uint16_val = to_u16(val);
            break;
        case BRIDGE_TYPE_INT32:
            var.type = OPCUA_TYPE_INT32;
            var.value.int32_val = to_i32(val);
            break;
        case BRIDGE_TYPE_UINT32:
            var.type = OPCUA_TYPE_UINT32;
            var.value.uint32_val = to_u32(val);
            break;
        case BRIDGE_TYPE_FLOAT:
            var.type = OPCUA_TYPE_FLOAT;
            var.value.float_val = (float)val;
            break;
        default:
            return false;
        }
        return opcua_server_write_value(router->opcua_server, &ep->target.opcua_node, &var);
    }

    case BRIDGE_PROTO_EIP_ASSEMBLY: {
        if (!router->eip_adapter)
            return false;
        uint8_t assem[EIP_MAX_ASSEMBLY_SIZE] = {0};
        size_t assem_sz = 0;
        if (!eip_adapter_get_assembly_data(router->eip_adapter, ep->target.eip.instance_id, assem,
                                           &assem_sz)) {
            return false;
        }
        /* アセンブリの宣言サイズを超える書き込みは失敗 (旧版は黙って切り捨てて成功扱いだった) */
        if (ep->target.eip.offset + sz > assem_sz)
            return false;
        memcpy(assem + ep->target.eip.offset, buf, sz);
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
        if (dst_type == BRIDGE_TYPE_RAW_BYTES)
            return mqtt_client_publish(router->mqtt_client, ep->target.mqtt_topic, buf, sz,
                                       MQTT_QOS_0, false);
        char payload[64] = {0};
        if (dst_type == BRIDGE_TYPE_FLOAT) {
            snprintf(payload, sizeof(payload), "%.2f", val);
        } else if (dst_type == BRIDGE_TYPE_INT16 || dst_type == BRIDGE_TYPE_INT32) {
            snprintf(payload, sizeof(payload), "%d", to_i32(val));
        } else {
            snprintf(payload, sizeof(payload), "%u", to_u32(val));
        }
        return mqtt_client_publish(router->mqtt_client, ep->target.mqtt_topic, payload,
                                   strlen(payload), MQTT_QOS_0, false);
    }

    default:
        return false;
    }
}

/* 変換後の値を宛先型のバイト列にする。生バイトは src をそのまま使う */
static size_t encode_value(const bridge_rule_t *rule, double final_val, const uint8_t *src_buf,
                           uint8_t *dst_buf) {
    switch (rule->dst_type) {
    case BRIDGE_TYPE_BOOL:
        dst_buf[0] = (final_val != 0.0) ? 1 : 0;
        return 1;
    case BRIDGE_TYPE_INT16: {
        int16_t v = to_i16(final_val);
        memcpy(dst_buf, &v, 2);
        return 2;
    }
    case BRIDGE_TYPE_UINT16: {
        uint16_t v = to_u16(final_val);
        memcpy(dst_buf, &v, 2);
        return 2;
    }
    case BRIDGE_TYPE_INT32: {
        int32_t v = to_i32(final_val);
        memcpy(dst_buf, &v, 4);
        return 4;
    }
    case BRIDGE_TYPE_UINT32: {
        uint32_t v = to_u32(final_val);
        memcpy(dst_buf, &v, 4);
        return 4;
    }
    case BRIDGE_TYPE_FLOAT: {
        float v = (float)final_val;
        memcpy(dst_buf, &v, 4);
        return 4;
    }
    default: /* BRIDGE_TYPE_RAW_BYTES: 長さは data_size (旧版は常に 2 バイトだった) */
        memcpy(dst_buf, src_buf, rule->data_size);
        return rule->data_size;
    }
}

static double scaled(const bridge_rule_t *rule, double raw) {
    /* scale == 0 は「変換なし」(互換のため。offset も適用しない) */
    return rule->scale != 0.0f ? raw * (double)rule->scale + (double)rule->offset : raw;
}

/* 1: 転送した, 0: 変化なしで省略, -1: 失敗 */
static int sync_rule(bridge_router_t *router, bridge_rule_t *rule) {
    if (!router || !rule || !rule->is_active)
        return 0;

    uint8_t src_buf[BRIDGE_MAX_DATA_SIZE] = {0};
    double raw_val = 0.0;
    if (!read_endpoint(router, &rule->src, rule->src_type, rule->data_size, src_buf, &raw_val)) {
        rule->error_count++;
        return -1;
    }
    const double final_val = scaled(rule, raw_val);

    if (rule->sync_mode == BRIDGE_SYNC_ON_CHANGE && rule->has_last_data) {
        if (rule->deadband > 0.0f && rule->src_type != BRIDGE_TYPE_RAW_BYTES) {
            /* 不感帯は宛先の単位 (スケール変換後) で比べる */
            if (fabs(final_val - rule->last_value) <= (double)rule->deadband)
                return 0;
        } else if (memcmp(rule->last_data, src_buf, rule->data_size) == 0) {
            return 0;
        }
    }

    uint8_t dst_buf[BRIDGE_MAX_DATA_SIZE] = {0};
    const size_t dst_sz = encode_value(rule, final_val, src_buf, dst_buf);
    if (!write_endpoint(router, &rule->dst, rule->dst_type, dst_sz, dst_buf, final_val)) {
        rule->error_count++;
        return -1;
    }
    memcpy(rule->last_data, src_buf, rule->data_size);
    rule->last_value = final_val;
    rule->has_last_data = true;
    rule->sync_count++;
    return 1;
}

/* MQTT が送信元のルールは受信時 (bridge_router_on_mqtt_message) にだけ動く */
static bool polled(const bridge_rule_t *rule) {
    return rule->is_active && rule->src.proto != BRIDGE_PROTO_MQTT_TOPIC;
}

size_t bridge_router_sync_all(bridge_router_t *router) {
    if (!router)
        return 0;
    size_t count = 0;
    for (size_t i = 0; i < router->rule_count; ++i)
        if (polled(&router->rules[i]) && sync_rule(router, &router->rules[i]) > 0)
            count++;
    return count;
}

size_t bridge_router_tick(bridge_router_t *router, uint32_t elapsed_ms) {
    if (!router)
        return 0;
    size_t count = 0;
    for (size_t i = 0; i < router->rule_count; ++i) {
        bridge_rule_t *rule = &router->rules[i];
        if (!polled(rule))
            continue;
        if (rule->sync_mode == BRIDGE_SYNC_PERIODIC) {
            rule->timer_ms += elapsed_ms;
            if (rule->interval_ms != 0 && rule->timer_ms < rule->interval_ms)
                continue;
            /* 周期の位相を保つ。遅れて呼ばれても 1 回だけ転送する */
            rule->timer_ms = rule->interval_ms ? rule->timer_ms % rule->interval_ms : 0;
        }
        if (sync_rule(router, rule) > 0)
            count++;
    }
    return count;
}

bool bridge_router_set_rule_active(bridge_router_t *router, int index, bool active) {
    if (!router || index < 0 || (size_t)index >= router->rule_count)
        return false;
    bridge_rule_t *rule = &router->rules[index];
    if (active && !rule->is_active) {
        rule->has_last_data = false; /* 再開時は最初の値を必ず転送する */
        rule->timer_ms = 0;
    }
    rule->is_active = active;
    return true;
}

const bridge_rule_t *bridge_router_get_rule(const bridge_router_t *router, int index) {
    if (!router || index < 0 || (size_t)index >= router->rule_count)
        return NULL;
    return &router->rules[index];
}

static bool parse_number(const void *payload, size_t len, double *out) {
    char text[32];
    if (!payload || len == 0 || len >= sizeof(text))
        return false;
    memcpy(text, payload, len);
    text[len] = '\0';
    char *p = text;
    while (isspace((unsigned char)*p))
        ++p;
    char *end = p + strlen(p);
    while (end > p && isspace((unsigned char)end[-1]))
        *--end = '\0';
    for (char *c = p; *c; ++c)
        *c = (char)tolower((unsigned char)*c);
    if (strcmp(p, "true") == 0 || strcmp(p, "on") == 0) {
        *out = 1.0;
        return true;
    }
    if (strcmp(p, "false") == 0 || strcmp(p, "off") == 0) {
        *out = 0.0;
        return true;
    }
    char *stop = NULL;
    const double v = strtod(p, &stop);
    if (stop == p || *stop != '\0' || v != v || v > 1e300 || v < -1e300)
        return false;
    *out = v;
    return true;
}

size_t bridge_router_on_mqtt_message(bridge_router_t *router, const char *topic,
                                     const void *payload, size_t payload_len) {
    if (!router || !topic || (!payload && payload_len))
        return 0;
    size_t count = 0;
    for (size_t i = 0; i < router->rule_count; ++i) {
        bridge_rule_t *rule = &router->rules[i];
        if (!rule->is_active || rule->src.proto != BRIDGE_PROTO_MQTT_TOPIC ||
            strncmp(rule->src.target.mqtt_topic, topic, BRIDGE_MAX_TOPIC_LEN) != 0)
            continue;
        uint8_t src_buf[BRIDGE_MAX_DATA_SIZE] = {0};
        double raw = 0.0;
        if (rule->src_type == BRIDGE_TYPE_RAW_BYTES) {
            if (payload_len != rule->data_size) {
                rule->error_count++;
                continue;
            }
            memcpy(src_buf, payload, payload_len);
        } else if (!parse_number(payload, payload_len, &raw)) {
            rule->error_count++;
            continue;
        }
        const double final_val = scaled(rule, raw);
        uint8_t dst_buf[BRIDGE_MAX_DATA_SIZE] = {0};
        const size_t dst_sz = encode_value(rule, final_val, src_buf, dst_buf);
        if (!write_endpoint(router, &rule->dst, rule->dst_type, dst_sz, dst_buf, final_val)) {
            rule->error_count++;
            continue;
        }
        rule->last_value = final_val;
        rule->has_last_data = true;
        rule->sync_count++;
        count++;
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
