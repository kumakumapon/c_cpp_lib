#pragma once
#ifndef EMBED_BRIDGE_HPP
#define EMBED_BRIDGE_HPP

#include "bridge_types.h"
#include "bridge_router.h"
#include <cstring>
#include <string>
#include <vector>

namespace embed_bridge {

class Router {
public:
    Router() {
        bridge_router_init(&router_);
    }

    void bind_modbus(uint16_t *holding_regs, size_t reg_count) {
        bridge_router_bind_modbus(&router_, holding_regs, reg_count);
    }

    void bind_opcua(opcua_server_t *server) {
        bridge_router_bind_opcua(&router_, server);
    }

    void bind_eip(eip_adapter_t *adapter) {
        bridge_router_bind_eip(&router_, adapter);
    }

    void bind_canopen(canopen_node_t *node) {
        bridge_router_bind_canopen(&router_, node);
    }

    void bind_mqtt(mqtt_client_t *client) {
        bridge_router_bind_mqtt(&router_, client);
    }

    int add_rule(const bridge_rule_t &rule) {
        return bridge_router_add_rule(&router_, &rule);
    }

    int map_modbus_to_opcua(uint16_t modbus_addr, const opcua_nodeid_t &node_id,
                            bridge_data_type_t dst_type = BRIDGE_TYPE_FLOAT,
                            float scale = 1.0f, float offset = 0.0f,
                            bridge_sync_mode_t sync_mode = BRIDGE_SYNC_ALWAYS,
                            uint32_t interval_ms = 0) {
        bridge_rule_t rule{};
        rule.src.proto = BRIDGE_PROTO_MODBUS_REG;
        rule.src.target.modbus_addr = modbus_addr;
        rule.src_type = BRIDGE_TYPE_UINT16;
        rule.dst.proto = BRIDGE_PROTO_OPCUA_NODE;
        rule.dst.target.opcua_node = node_id;
        rule.dst_type = dst_type;
        rule.scale = scale;
        rule.offset = offset;
        rule.sync_mode = sync_mode;
        rule.interval_ms = interval_ms;
        return bridge_router_add_rule(&router_, &rule);
    }

    int map_opcua_to_modbus(const opcua_nodeid_t &node_id, uint16_t modbus_addr,
                            bridge_data_type_t src_type = BRIDGE_TYPE_FLOAT,
                            float scale = 1.0f, float offset = 0.0f,
                            bridge_sync_mode_t sync_mode = BRIDGE_SYNC_ALWAYS,
                            uint32_t interval_ms = 0) {
        bridge_rule_t rule{};
        rule.src.proto = BRIDGE_PROTO_OPCUA_NODE;
        rule.src.target.opcua_node = node_id;
        rule.src_type = src_type;
        rule.dst.proto = BRIDGE_PROTO_MODBUS_REG;
        rule.dst.target.modbus_addr = modbus_addr;
        rule.dst_type = BRIDGE_TYPE_UINT16;
        rule.scale = scale;
        rule.offset = offset;
        rule.sync_mode = sync_mode;
        rule.interval_ms = interval_ms;
        return bridge_router_add_rule(&router_, &rule);
    }

    int map_eip_to_mqtt(uint16_t instance_id, uint16_t offset, const std::string &topic,
                        bridge_data_type_t type = BRIDGE_TYPE_FLOAT,
                        bridge_sync_mode_t sync_mode = BRIDGE_SYNC_ALWAYS,
                        uint32_t interval_ms = 0) {
        bridge_rule_t rule{};
        rule.src.proto = BRIDGE_PROTO_EIP_ASSEMBLY;
        rule.src.target.eip.instance_id = instance_id;
        rule.src.target.eip.offset = offset;
        rule.src_type = type;
        rule.dst.proto = BRIDGE_PROTO_MQTT_TOPIC;
        strncpy(rule.dst.target.mqtt_topic, topic.c_str(), sizeof(rule.dst.target.mqtt_topic) - 1);
        rule.dst_type = type;
        rule.scale = 1.0f;
        rule.offset = 0.0f;
        rule.sync_mode = sync_mode;
        rule.interval_ms = interval_ms;
        return bridge_router_add_rule(&router_, &rule);
    }

    int map_canopen_to_modbus(uint16_t index, uint8_t subindex, uint16_t modbus_addr,
                              bridge_data_type_t src_type = BRIDGE_TYPE_INT32,
                              bridge_sync_mode_t sync_mode = BRIDGE_SYNC_ALWAYS,
                              uint32_t interval_ms = 0) {
        bridge_rule_t rule{};
        rule.src.proto = BRIDGE_PROTO_CANOPEN_OD;
        rule.src.target.canopen.index = index;
        rule.src.target.canopen.subindex = subindex;
        rule.src_type = src_type;
        rule.dst.proto = BRIDGE_PROTO_MODBUS_REG;
        rule.dst.target.modbus_addr = modbus_addr;
        rule.dst_type = BRIDGE_TYPE_UINT16;
        rule.scale = 1.0f;
        rule.offset = 0.0f;
        rule.sync_mode = sync_mode;
        rule.interval_ms = interval_ms;
        return bridge_router_add_rule(&router_, &rule);
    }

    size_t sync_all() {
        return bridge_router_sync_all(&router_);
    }

    size_t tick(uint32_t elapsed_ms) {
        return bridge_router_tick(&router_, elapsed_ms);
    }

    bridge_router_t* raw_handle() { return &router_; }
    const bridge_router_t* raw_handle() const { return &router_; }

private:
    bridge_router_t router_{};
};

} // namespace embed_bridge

#endif /* EMBED_BRIDGE_HPP */
