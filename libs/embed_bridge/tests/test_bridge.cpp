#include "embed_test/embed_test.hpp"
#include <cmath>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "embed_bridge/bridge_router.h"
#include "embed_bridge/bridge_types.h"
#include "embed_bridge/embed_bridge.hpp"
#include "embed_canopen/canopen_node.h"
#include "embed_eip/eip_adapter.h"
#include "embed_mqtt/mqtt_client.h"
#include "embed_opcua/opcua_server.h"

static void mock_mqtt_send(const uint8_t *data, size_t len, void *user_data) {
    (void)data;
    (void)len;
    (void)user_data;
}

TEST_CASE(test_bridge_modbus_to_opcua) {
    std::cout << "# [Test] Running Modbus -> OPC-UA bridge tests..." << std::endl;

    uint16_t modbus_regs[10] = {0};
    modbus_regs[2] = 250; // 25.0 degC * 10

    opcua_server_t server;
    opcua_server_init(&server, "opc.tcp://localhost:4840");

    opcua_nodeid_t temp_node{};
    temp_node.id_type = OPCUA_NODEID_NUMERIC;
    temp_node.namespace_index = 1;
    temp_node.id.numeric = 2001;

    opcua_variant_t init_val{};
    init_val.type = OPCUA_TYPE_FLOAT;
    init_val.value.float_val = 0.0f;
    opcua_server_add_variable(&server, &temp_node, "Temperature", &init_val, 0x03);

    embed_bridge::Router router;
    router.bind_modbus(modbus_regs, 10);
    router.bind_opcua(&server);

    // Modbus 40003 (reg 2) -> OPC-UA Node 2001, scale=0.1, offset=0.0
    router.map_modbus_to_opcua(2, temp_node, BRIDGE_TYPE_FLOAT, 0.1f, 0.0f);

    size_t synced = router.sync_all();
    CHECK(synced == 1);

    opcua_datavalue_t dv{};
    CHECK(opcua_server_read_value(&server, &temp_node, &dv));
    CHECK(dv.value.type == OPCUA_TYPE_FLOAT);
    CHECK(fabsf(dv.value.value.float_val - 25.0f) < 0.001f);

    std::cout << "# [Test] Modbus -> OPC-UA bridge passed!" << std::endl;
}

TEST_CASE(test_bridge_canopen_and_eip_to_mqtt) {
    std::cout << "# [Test] Running CANopen & EIP -> MQTT bridge tests..." << std::endl;

    // 1. CANopen Setup
    canopen_node_t can_node;
    canopen_node_init(&can_node, 1, "Drive1");
    int32_t motor_speed = 1500;
    canopen_od_add_entry(&can_node.od, 0x606C, 0x00, OD_TYPE_INT32, OD_ACCESS_RW, &motor_speed,
                         sizeof(motor_speed));

    // 2. EtherNet/IP Setup
    cip_identity_attr_t id{};
    id.vendor_id = 0x002A;
    strncpy(id.product_name, "EIP-Bridge", sizeof(id.product_name) - 1);
    eip_adapter_t eip_adapter;
    eip_adapter_init(&eip_adapter, &id);
    eip_adapter_add_assembly(&eip_adapter, 100, 16);

    float flow_rate = 123.45f;
    eip_adapter_set_assembly_data(&eip_adapter, 100, &flow_rate, sizeof(float));

    // 3. MQTT Setup
    mqtt_client_t mqtt_client;
    mqtt_client_init(&mqtt_client, mock_mqtt_send, nullptr);
    mqtt_client.state = MQTT_STATE_CONNECTED;

    // 4. Modbus Setup
    uint16_t modbus_regs[10] = {0};

    // 5. Bridge Setup
    embed_bridge::Router router;
    router.bind_canopen(&can_node);
    router.bind_eip(&eip_adapter);
    router.bind_mqtt(&mqtt_client);
    router.bind_modbus(modbus_regs, 10);

    // CANopen 0x606C.0 (Speed INT32) -> Modbus Reg 5 (UINT16)
    router.map_canopen_to_modbus(0x606C, 0x00, 5, BRIDGE_TYPE_INT32);

    // EIP Assembly 100 Offset 0 -> MQTT topic "sensors/flow"
    router.map_eip_to_mqtt(100, 0, "sensors/flow", BRIDGE_TYPE_FLOAT);

    size_t count = router.sync_all();
    CHECK(count == 2);

    // Verify Modbus reg 5 has motor speed 1500
    CHECK(modbus_regs[5] == 1500);

    std::cout << "# [Test] CANopen & EIP -> MQTT bridge passed!" << std::endl;
}

TEST_CASE(test_bridge_periodic_and_cov) {
    std::cout << "# [Test] Running Periodic and COV synchronization tests..." << std::endl;

    float raw_var = 10.0f;
    uint16_t modbus_regs[5] = {0};

    embed_bridge::Router router;
    router.bind_modbus(modbus_regs, 5);

    // COV mode rule: raw_var -> Modbus reg 0
    bridge_rule_t rule_cov{};
    rule_cov.src.proto = BRIDGE_PROTO_RAW_VAR;
    rule_cov.src.target.raw_ptr = &raw_var;
    rule_cov.src_type = BRIDGE_TYPE_FLOAT;
    rule_cov.dst.proto = BRIDGE_PROTO_MODBUS_REG;
    rule_cov.dst.target.modbus_addr = 0;
    rule_cov.dst_type = BRIDGE_TYPE_UINT16;
    rule_cov.scale = 1.0f;
    rule_cov.sync_mode = BRIDGE_SYNC_ON_CHANGE;
    router.add_rule(rule_cov);

    // Periodic mode rule: raw_var -> Modbus reg 1 (interval 100ms)
    bridge_rule_t rule_periodic{};
    rule_periodic.src.proto = BRIDGE_PROTO_RAW_VAR;
    rule_periodic.src.target.raw_ptr = &raw_var;
    rule_periodic.src_type = BRIDGE_TYPE_FLOAT;
    rule_periodic.dst.proto = BRIDGE_PROTO_MODBUS_REG;
    rule_periodic.dst.target.modbus_addr = 1;
    rule_periodic.dst_type = BRIDGE_TYPE_UINT16;
    rule_periodic.scale = 2.0f; // raw * 2
    rule_periodic.sync_mode = BRIDGE_SYNC_PERIODIC;
    rule_periodic.interval_ms = 100;
    router.add_rule(rule_periodic);

    // 1st tick: COV detects initial change, Periodic at 50ms does not fire
    size_t n = router.tick(50);
    CHECK(n == 1); // Only COV synced
    CHECK(modbus_regs[0] == 10);
    CHECK(modbus_regs[1] == 0); // Periodic hasn't fired yet

    // 2nd tick: 50ms more (total 100ms) -> Periodic fires!
    n = router.tick(50);
    CHECK(n == 1);               // Only Periodic synced (COV didn't change)
    CHECK(modbus_regs[1] == 20); // 10 * 2

    // Now change raw_var
    raw_var = 42.0f;
    n = router.tick(10);
    CHECK(n == 1); // COV fired
    CHECK(modbus_regs[0] == 42);

    std::cout << "# [Test] Periodic and COV synchronization passed!" << std::endl;
}

TEST_CASE(modbus_tcp_register_bank) {
    bridge_router_t router{};
    bridge_router_init(&router);
    uint16_t regs[2] = {10, 20};
    bridge_router_bind_modbus(&router, regs, 2);
    modbus_slave_t slave{};
    REQUIRE(bridge_router_init_modbus_slave(&router, &slave, 1));
    uint8_t req[] = {0, 42, 0, 0, 0, 6, 1, 6, 0, 1, 0, 99}, resp[260];
    CHECK_EQ(embed_modbus_tcp_handle(&slave, req, sizeof(req), resp, sizeof(resp)), 12u);
    CHECK_EQ(regs[1], 99u);
    req[7] = 3;
    req[11] = 1;
    CHECK_EQ(embed_modbus_tcp_handle(&slave, req, sizeof(req), resp, sizeof(resp)), 11u);
    CHECK_EQ(resp[10], 99u);
    req[9] = 2;
    CHECK_EQ(embed_modbus_tcp_handle(&slave, req, sizeof(req), resp, sizeof(resp)), 9u);
    CHECK_EQ(resp[8], MODBUS_EX_ILLEGAL_DATA_ADDRESS);
}
EMBED_TEST_MAIN()
