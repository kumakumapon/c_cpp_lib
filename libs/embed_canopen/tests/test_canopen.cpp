#include <iostream>
#include <cassert>
#include <cstring>
#include "embed_canopen/canopen_types.h"
#include "embed_canopen/canopen_od.h"
#include "embed_canopen/canopen_node.h"
#include "embed_canopen/embed_canopen.hpp"

void test_canopen_od_basic() {
    std::cout << "[Test] Running CANopen OD basic tests..." << std::endl;
    canopen_od_t od;
    canopen_od_init(&od);

    uint32_t val32 = 0x12345678;
    uint16_t val16 = 0xABCD;

    assert(canopen_od_add_entry(&od, 0x2000, 0x01, OD_TYPE_UINT32, OD_ACCESS_RW, &val32, sizeof(val32)));
    assert(canopen_od_add_entry(&od, 0x2000, 0x02, OD_TYPE_UINT16, OD_ACCESS_RO, &val16, sizeof(val16)));

    // Read test
    uint32_t read_val = 0;
    size_t out_len = 0;
    assert(canopen_od_read(&od, 0x2000, 0x01, &read_val, sizeof(read_val), &out_len));
    assert(read_val == 0x12345678);
    assert(out_len == 4);

    // Write test
    uint32_t new_val = 0x87654321;
    assert(canopen_od_write(&od, 0x2000, 0x01, &new_val, sizeof(new_val)));
    assert(val32 == 0x87654321);

    // Write to RO should fail
    uint16_t new_val16 = 0x5555;
    assert(!canopen_od_write(&od, 0x2000, 0x02, &new_val16, sizeof(new_val16)));
    assert(val16 == 0xABCD);

    std::cout << "[Test] OD basic tests passed!" << std::endl;
}

void test_canopen_node_nmt_and_heartbeat() {
    std::cout << "[Test] Running CANopen Node NMT & Heartbeat tests..." << std::endl;
    canopen_node_t node;
    canopen_node_init(&node, 5, "TestNode");

    assert(node.node_id == 5);
    assert(node.nmt_state == CANOPEN_NMT_STATE_PRE_OPERATIONAL);

    // Heartbeat tick
    can_frame_t tx{};
    bool sent = canopen_node_timer_tick(&node, 500, &tx);
    assert(!sent); // 500ms < 1000ms

    sent = canopen_node_timer_tick(&node, 500, &tx);
    assert(sent);
    assert(tx.id == 0x700 + 5);
    assert(tx.len == 1);
    assert(tx.data[0] == CANOPEN_NMT_STATE_PRE_OPERATIONAL);

    // NMT Start command to Node 5
    can_frame_t nmt_cmd{};
    nmt_cmd.id = 0x000;
    nmt_cmd.len = 2;
    nmt_cmd.data[0] = CANOPEN_NMT_CMD_START;
    nmt_cmd.data[1] = 5;
    canopen_node_process_frame(&node, &nmt_cmd, &tx);
    assert(node.nmt_state == CANOPEN_NMT_STATE_OPERATIONAL);

    // Heartbeat in operational state
    sent = canopen_node_timer_tick(&node, 1000, &tx);
    assert(sent);
    assert(tx.data[0] == CANOPEN_NMT_STATE_OPERATIONAL);

    // NMT Stop command
    nmt_cmd.data[0] = CANOPEN_NMT_CMD_STOP;
    canopen_node_process_frame(&node, &nmt_cmd, &tx);
    assert(node.nmt_state == CANOPEN_NMT_STATE_STOPPED);

    std::cout << "[Test] Node NMT & Heartbeat tests passed!" << std::endl;
}

void test_canopen_sdo_transfer() {
    std::cout << "[Test] Running CANopen SDO Read/Write tests..." << std::endl;
    canopen_node_t node;
    canopen_node_init(&node, 1, "ServoDrive");

    uint32_t target_position = 10000;
    canopen_od_add_entry(&node.od, 0x607A, 0x00, OD_TYPE_INT32, OD_ACCESS_RW, &target_position, sizeof(target_position));

    // 1. SDO Upload Request (Read 0x607A.0)
    can_frame_t rx_req{};
    rx_req.id = 0x600 + 1;
    rx_req.len = 8;
    rx_req.data[0] = 0x40; // Initiate Upload Request
    rx_req.data[1] = 0x7A;
    rx_req.data[2] = 0x60;
    rx_req.data[3] = 0x00;

    can_frame_t tx_res{};
    bool ok = canopen_node_process_frame(&node, &rx_req, &tx_res);
    assert(ok);
    assert(tx_res.id == 0x580 + 1);
    assert(tx_res.len == 8);
    assert((tx_res.data[0] & 0xE0) == 0x40); // Upload response
    uint32_t resp_val = 0;
    memcpy(&resp_val, &tx_res.data[4], 4);
    assert(resp_val == 10000);

    // 2. SDO Download Request (Write 25000 to 0x607A.0)
    rx_req.data[0] = 0x23; // Initiate Download Request (4 bytes: n=0)
    rx_req.data[1] = 0x7A;
    rx_req.data[2] = 0x60;
    rx_req.data[3] = 0x00;
    uint32_t new_pos = 25000;
    memcpy(&rx_req.data[4], &new_pos, 4);

    ok = canopen_node_process_frame(&node, &rx_req, &tx_res);
    assert(ok);
    assert(tx_res.id == 0x580 + 1);
    assert(tx_res.data[0] == 0x60); // Download success response
    assert(target_position == 25000);

    // 3. SDO Read Non-Existent Object -> SDO Abort (0x80)
    rx_req.data[0] = 0x40;
    rx_req.data[1] = 0xFF;
    rx_req.data[2] = 0xFF;
    rx_req.data[3] = 0x00;
    ok = canopen_node_process_frame(&node, &rx_req, &tx_res);
    assert(ok);
    assert(tx_res.data[0] == 0x80); // Abort

    std::cout << "[Test] SDO Read/Write tests passed!" << std::endl;
}

void test_canopen_cpp_wrapper() {
    std::cout << "[Test] Running CANopen C++ wrapper tests..." << std::endl;
    embed_canopen::Node node(3, "SensorHub");

    float temperature = 24.5f;
    node.add_variable(0x2100, 0x01, temperature);

    float read_t = 0.0f;
    assert(node.read_od(0x2100, 0x01, read_t));
    assert(read_t == 24.5f);

    node.write_od(0x2100, 0x01, 36.8f);
    assert(temperature == 36.8f);

    // PDO test: Operational state required
    can_frame_t tx{};
    assert(!node.send_tpdo1((const uint8_t*)"1234", 4, tx)); // Pre-op should reject PDO

    // Start node
    can_frame_t nmt_cmd{};
    nmt_cmd.id = 0x000;
    nmt_cmd.len = 2;
    nmt_cmd.data[0] = CANOPEN_NMT_CMD_START;
    nmt_cmd.data[1] = 3;
    node.process_frame(nmt_cmd, tx);
    assert(node.nmt_state() == CANOPEN_NMT_STATE_OPERATIONAL);

    // Send TPDO1 now
    uint8_t pdo_data[4] = {0x11, 0x22, 0x33, 0x44};
    assert(node.send_tpdo1(pdo_data, sizeof(pdo_data), tx));
    assert(tx.id == 0x180 + 3);
    assert(tx.len == 4);
    assert(tx.data[0] == 0x11);
    assert(tx.data[3] == 0x44);

    std::cout << "[Test] C++ wrapper tests passed!" << std::endl;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << " Running embed_canopen test suite..." << std::endl;
    std::cout << "========================================" << std::endl;

    test_canopen_od_basic();
    test_canopen_node_nmt_and_heartbeat();
    test_canopen_sdo_transfer();
    test_canopen_cpp_wrapper();

    std::cout << "========================================" << std::endl;
    std::cout << " All embed_canopen tests PASSED!" << std::endl;
    std::cout << "========================================" << std::endl;
    return 0;
}
