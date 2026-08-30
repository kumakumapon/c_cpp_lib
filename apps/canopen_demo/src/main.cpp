#include <iostream>
#include <iomanip>
#include <vector>

#include "embed_canopen/canopen_types.h"
#include "embed_canopen/canopen_od.h"
#include "embed_canopen/canopen_node.h"
#include "embed_canopen/embed_canopen.hpp"

static void print_can_frame(const char *prefix, const can_frame_t &frame) {
    std::cout << prefix << " [COB-ID: 0x" << std::hex << std::uppercase << std::setw(3) << std::setfill('0')
              << frame.id << std::dec << " DLC: " << (int)frame.len << " Data: ";
    for (int i = 0; i < frame.len; ++i) {
        std::cout << "0x" << std::hex << std::setw(2) << std::setfill('0') << (int)frame.data[i] << " ";
    }
    std::cout << std::dec << "]" << std::endl;
}

int main() {
    std::cout << "=====================================================" << std::endl;
    std::cout << " CANopen Protocol Stack (embed_canopen) Demo" << std::endl;
    std::cout << "=====================================================" << std::endl;

    // -----------------------------------------------------------------
    // Scenario 1: ノード初期化 & NMT 状態管理 (Node ID: 1)
    // -----------------------------------------------------------------
    std::cout << "\n>>> Scenario 1: CANopen Node Initialization & NMT Transitions <<<" << std::endl;
    embed_canopen::Node node(1, "ServoDrive-CiA402");

    int32_t target_pos = 0;
    int32_t actual_pos = 12500;
    node.add_variable(0x607A, 0x00, target_pos, OD_ACCESS_RW); // Target Position
    node.add_variable(0x6064, 0x00, actual_pos, OD_ACCESS_RO); // Actual Position

    std::cout << "  Node initialized (ID: " << (int)node.node_id() << ")" << std::endl;
    std::cout << "  Current NMT State: 0x" << std::hex << (int)node.nmt_state() << " (Pre-Operational)" << std::dec << std::endl;

    can_frame_t tx{};
    if (node.timer_tick(1000, tx)) {
        print_can_frame("  [Heartbeat]", tx);
    }

    // -----------------------------------------------------------------
    // Scenario 2: SDO によるパラメータ読み書き (SDO Upload & Download)
    // -----------------------------------------------------------------
    std::cout << "\n>>> Scenario 2: SDO Communication (Client -> Slave) <<<" << std::endl;

    // SDO Upload: Read Actual Position (0x6064.0)
    can_frame_t sdo_read{};
    sdo_read.id = 0x600 + 1; // RSDO
    sdo_read.len = 8;
    sdo_read.data[0] = 0x40; // Read Request
    sdo_read.data[1] = 0x64; // Index 0x6064
    sdo_read.data[2] = 0x60;
    sdo_read.data[3] = 0x00; // SubIndex 0
    print_can_frame("  Master -> Node1 (SDO Read 0x6064.0): ", sdo_read);

    if (node.process_frame(sdo_read, tx)) {
        print_can_frame("  Node1 -> Master (SDO Read Response): ", tx);
    }

    // SDO Download: Write Target Position (0x607A.0 = 50000)
    can_frame_t sdo_write{};
    sdo_write.id = 0x600 + 1;
    sdo_write.len = 8;
    sdo_write.data[0] = 0x23; // Write Request 4 bytes
    sdo_write.data[1] = 0x7A; // Index 0x607A
    sdo_write.data[2] = 0x60;
    sdo_write.data[3] = 0x00;
    int32_t new_target = 50000;
    memcpy(&sdo_write.data[4], &new_target, 4);
    print_can_frame("  Master -> Node1 (SDO Write 0x607A.0 = 50000): ", sdo_write);

    if (node.process_frame(sdo_write, tx)) {
        print_can_frame("  Node1 -> Master (SDO Write Response): ", tx);
        std::cout << "  Updated Target Position in OD: " << target_pos << std::endl;
    }

    // -----------------------------------------------------------------
    // Scenario 3: NMT Operational 移行 & 高速 TPDO1 送信
    // -----------------------------------------------------------------
    std::cout << "\n>>> Scenario 3: Operational State & Real-Time TPDO1 <<<" << std::endl;

    can_frame_t nmt_start{};
    nmt_start.id = 0x000;
    nmt_start.len = 2;
    nmt_start.data[0] = CANOPEN_NMT_CMD_START;
    nmt_start.data[1] = 1;
    print_can_frame("  Master -> Network (NMT Start Node 1): ", nmt_start);
    node.process_frame(nmt_start, tx);

    std::cout << "  Current NMT State: 0x" << std::hex << (int)node.nmt_state() << " (Operational)" << std::dec << std::endl;

    // Send TPDO1 with actual position
    if (node.send_tpdo1((const uint8_t*)&actual_pos, sizeof(actual_pos), tx)) {
        print_can_frame("  [TPDO1 Transmit (Actual Pos)]", tx);
    }

    std::cout << "\n=====================================================" << std::endl;
    std::cout << " embed_canopen demonstration completed successfully!" << std::endl;
    std::cout << "=====================================================" << std::endl;
    return 0;
}
