#include <iostream>
#include <iomanip>
#include <vector>

#include "embed_bridge/embed_bridge.hpp"
#include "embed_opcua/embed_opcua.hpp"
#include "embed_eip/embed_eip.hpp"
#include "embed_canopen/embed_canopen.hpp"
#include "embed_mqtt/mqtt_client.h"

static void mock_mqtt_send(const uint8_t *data, size_t len, void *user_data) {
    (void)data; (void)user_data;
    std::cout << "  [MQTT Network Tx] " << len << " bytes sent to broker" << std::endl;
}

int main() {
    std::cout << "=====================================================" << std::endl;
    std::cout << " Industrial Multi-Protocol Router (embed_bridge) Demo" << std::endl;
    std::cout << "=====================================================" << std::endl;

    // 1. 各プロトコルインスタンスの準備
    std::cout << "\n>>> 1. Initializing Protocol Adapters & Nodes <<<" << std::endl;

    // (A) Modbus テーブル (40001〜40010)
    uint16_t modbus_regs[10] = {0};
    modbus_regs[0] = 235; // Modbus Reg 0: 23.5 degC (x10)
    std::cout << "  [Modbus] Holding Registers array bound (10 regs)" << std::endl;

    // (B) OPC-UA サーバ
    embed_opcua::Server opcua_server("opc.tcp://localhost:4840");
    opcua_nodeid_t temp_node{};
    temp_node.id_type = OPCUA_NODEID_NUMERIC;
    temp_node.namespace_index = 1;
    temp_node.id.numeric = 2001;
    opcua_server.add_variable(1, 2001, "Heater_Temperature", 0.0);
    std::cout << "  [OPC-UA] Variable Node (ns=1;i=2001: Heater_Temperature) created" << std::endl;

    // (C) EtherNet/IP アダプタ
    embed_eip::Adapter eip_adapter(0x002A, 0x000C, 0x0001, "EIP-Sensor-Hub");
    eip_adapter.add_assembly(100, 16);
    float initial_flow = 84.5f;
    eip_adapter.set_assembly_pod(100, initial_flow);
    std::cout << "  [EtherNet/IP] Assembly Instance 100 configured (FlowRate = 84.5 L/min)" << std::endl;

    // (D) CANopen ノード
    embed_canopen::Node can_node(1, "Axis1-Drive");
    int32_t motor_rpm = 1750;
    can_node.add_variable(0x606C, 0x00, motor_rpm, OD_ACCESS_RO);
    std::cout << "  [CANopen] Node 1 configured (OD 0x606C.0 Actual Velocity = 1750 RPM)" << std::endl;

    // (E) MQTT クライアント
    mqtt_client_t mqtt_client;
    mqtt_client_init(&mqtt_client, mock_mqtt_send, nullptr);
    mqtt_client.state = MQTT_STATE_CONNECTED;
    std::cout << "  [MQTT] Client connected to broker" << std::endl;

    // 2. ルーターの設定 & マッピング登録
    std::cout << "\n>>> 2. Configuring Cross-Protocol Routing Rules <<<" << std::endl;
    embed_bridge::Router router;
    router.bind_modbus(modbus_regs, 10);
    router.bind_opcua(opcua_server.raw_handle());
    router.bind_eip(eip_adapter.raw_handle());
    router.bind_canopen(can_node.raw_handle());
    router.bind_mqtt(&mqtt_client);

    // Rule 1: Modbus Reg 0 (x10) -> OPC-UA Temp Node (Scale 0.1)
    router.map_modbus_to_opcua(0, temp_node, BRIDGE_TYPE_FLOAT, 0.1f, 0.0f);
    std::cout << "  Rule 1: Modbus Reg #0 (x10) -> OPC-UA ns=1;i=2001 (Scale: 0.1)" << std::endl;

    // Rule 2: CANopen Actual RPM (0x606C.0) -> Modbus Reg #5
    router.map_canopen_to_modbus(0x606C, 0x00, 5, BRIDGE_TYPE_UINT16);
    std::cout << "  Rule 2: CANopen OD 0x606C.0 (RPM) -> Modbus Reg #5" << std::endl;

    // Rule 3: EtherNet/IP Assembly 100 (Offset 0) -> MQTT "plant/line1/flow"
    router.map_eip_to_mqtt(100, 0, "plant/line1/flow", BRIDGE_TYPE_FLOAT, BRIDGE_SYNC_ON_CHANGE);
    std::cout << "  Rule 3: EtherNet/IP Assem 100 Offset 0 -> MQTT Topic 'plant/line1/flow'" << std::endl;

    // 3. ルーティング同期の実行
    std::cout << "\n>>> 3. Executing Router Synchronization <<<" << std::endl;
    size_t synced = router.sync_all();
    std::cout << "  Successfully synchronized " << synced << " rules across protocols!" << std::endl;

    // 結果の確認
    opcua_datavalue_t dv{};
    opcua_server.get_value(1, 2001, dv);
    std::cout << "  => OPC-UA Temperature Node Value: " << std::fixed << std::setprecision(1) << dv.value.value.float_val << " degC (Converted from Modbus 235)" << std::endl;
    std::cout << "  => Modbus Reg #5 Value: " << modbus_regs[5] << " RPM (Forwarded from CANopen OD)" << std::endl;

    // 4. 動的変更と Change-of-Value (COV) 同期
    std::cout << "\n>>> 4. Simulating Dynamic Sensor Change (Modbus: 235 -> 382) <<<" << std::endl;
    modbus_regs[0] = 382; // 38.2 degC
    router.tick(10);

    opcua_server.get_value(1, 2001, dv);
    std::cout << "  => OPC-UA Temperature Node Value (Updated): " << dv.value.value.float_val << " degC" << std::endl;

    std::cout << "\n=====================================================" << std::endl;
    std::cout << " embed_bridge demonstration completed successfully!" << std::endl;
    std::cout << "=====================================================" << std::endl;
    return 0;
}
