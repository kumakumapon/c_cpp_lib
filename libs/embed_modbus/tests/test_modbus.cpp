#include "embed_test/embed_test.hpp"
#include <cstring>
#include <iostream>
#include <vector>

#include "embed_modbus/embed_modbus.hpp"
#include "embed_modbus/modbus_crc.h"
#include "embed_modbus/modbus_master.h"
#include "embed_modbus/modbus_rtu.h"
#include "embed_modbus/modbus_slave.h"
#include "embed_modbus/modbus_types.h"

TEST_CASE(test_modbus_crc) {
    std::cout << "# [Test] Running Modbus CRC16 & LRC tests..." << std::endl;

    // 標準Modbus RTUフレーム例: Unit 1, FC 0x03, Start 0x0000, Count 0x000A
    // 期待CRC16: 0xCDC5 (フレーム末尾には 0xC5, 0xCD の順で格納される)
    const uint8_t msg[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x0A};
    uint16_t crc = modbus_crc16(msg, sizeof(msg));
    CHECK(crc == 0xCDC5);

    // LRCテスト
    uint8_t lrc = modbus_lrc(msg, sizeof(msg));
    CHECK(((uint8_t)(0x01 + 0x03 + 0x00 + 0x00 + 0x00 + 0x0A + lrc)) == 0);

    std::cout << "# [Test] Modbus CRC16 & LRC passed!" << std::endl;
}

// テスト用レジスタ領域
static uint16_t s_test_holding_regs[100];

static modbus_exception_t mock_read_holding(uint16_t addr, uint16_t count, uint16_t *dest, void *) {
    if (addr + count > 100)
        return MODBUS_EX_ILLEGAL_DATA_ADDRESS;
    for (uint16_t i = 0; i < count; ++i) {
        dest[i] = s_test_holding_regs[addr + i];
    }
    return MODBUS_EX_NONE;
}

static modbus_exception_t mock_write_holding(uint16_t addr, uint16_t count, const uint16_t *src,
                                             void *) {
    if (addr + count > 100)
        return MODBUS_EX_ILLEGAL_DATA_ADDRESS;
    for (uint16_t i = 0; i < count; ++i) {
        s_test_holding_regs[addr + i] = src[i];
    }
    return MODBUS_EX_NONE;
}

TEST_CASE(test_master_slave_holding_registers) {
    std::cout << "# [Test] Running Holding Registers Master/Slave tests..." << std::endl;

    for (int i = 0; i < 100; ++i) {
        s_test_holding_regs[i] = (uint16_t)(1000 + i);
    }

    modbus_slave_callbacks_t cbs{};
    cbs.read_holding_registers = mock_read_holding;
    cbs.write_holding_registers = mock_write_holding;

    modbus_slave_t slave;
    modbus_slave_init(&slave, 1, &cbs, nullptr);

    // 1. Master: FC 0x03 Read Holding Registers (Addr 10, Count 4)
    uint8_t req_pdu[MODBUS_MAX_PDU_LENGTH];
    size_t req_pdu_len = 0;
    bool ok = modbus_master_req_read_holding_registers(10, 4, req_pdu, &req_pdu_len);
    CHECK(ok);
    CHECK(req_pdu_len == 5);

    // 2. Slave: PDU処理
    uint8_t resp_pdu[MODBUS_MAX_PDU_LENGTH];
    size_t resp_pdu_len = 0;
    ok = modbus_slave_process_pdu(&slave, req_pdu, req_pdu_len, resp_pdu, &resp_pdu_len);
    CHECK(ok);
    CHECK(resp_pdu[0] == MODBUS_FC_READ_HOLDING_REGISTERS);
    CHECK(resp_pdu[1] == 8); // 4レジスタ × 2バイト = 8バイト

    // 3. Master: 応答デコード
    uint16_t read_values[4];
    modbus_status_t status =
        modbus_master_parse_read_registers_resp(resp_pdu, resp_pdu_len, read_values, 4);
    CHECK(status == MODBUS_OK);
    CHECK(read_values[0] == 1010);
    CHECK(read_values[1] == 1011);
    CHECK(read_values[2] == 1012);
    CHECK(read_values[3] == 1013);

    // 4. Master: FC 0x06 Write Single Register (Addr 20, Value 9999)
    ok = modbus_master_req_write_single_register(20, 9999, req_pdu, &req_pdu_len);
    CHECK(ok);
    ok = modbus_slave_process_pdu(&slave, req_pdu, req_pdu_len, resp_pdu, &resp_pdu_len);
    CHECK(ok);
    CHECK(s_test_holding_regs[20] == 9999);

    std::cout << "# [Test] Holding Registers Master/Slave passed!" << std::endl;
}

TEST_CASE(test_modbus_rtu_framing) {
    std::cout << "# [Test] Running Modbus RTU Framing tests..." << std::endl;

    modbus_slave_callbacks_t cbs{};
    cbs.read_holding_registers = mock_read_holding;
    cbs.write_holding_registers = mock_write_holding;

    modbus_slave_t slave;
    modbus_slave_init(&slave, 2, &cbs, nullptr); // Unit ID = 2

    // MasterリクエストPDU作成
    uint8_t req_pdu[5];
    size_t req_pdu_len = 0;
    modbus_master_req_read_holding_registers(0, 2, req_pdu, &req_pdu_len);

    // RTUフレームにエンコード (Unit ID 2)
    uint8_t req_adu[MODBUS_MAX_ADU_LENGTH];
    size_t req_adu_len = 0;
    bool ok = modbus_rtu_encode_frame(2, req_pdu, req_pdu_len, req_adu, &req_adu_len);
    CHECK(ok);
    CHECK(req_adu_len == 1 + 5 + 2); // 8バイト

    // Slaveが直接RTUフレームを受信・処理
    uint8_t resp_adu[MODBUS_MAX_ADU_LENGTH];
    size_t resp_adu_len = 0;
    ok = modbus_rtu_slave_handle(&slave, req_adu, req_adu_len, resp_adu, &resp_adu_len);
    CHECK(ok);
    CHECK(resp_adu[0] == 2); // Unit ID 2

    // CRC破壊テスト
    req_adu[req_adu_len - 1] ^= 0xFF;
    ok = modbus_rtu_slave_handle(&slave, req_adu, req_adu_len, resp_adu, &resp_adu_len);
    CHECK(!ok); // CRCエラーのため応答なし

    std::cout << "# [Test] Modbus RTU Framing passed!" << std::endl;
}

TEST_CASE(test_cpp_slave_wrapper) {
    std::cout << "# [Test] Running C++ Slave Wrapper tests..." << std::endl;

    embed_modbus::Slave slave(1);
    slave.set_holding_register(100, 12345);
    slave.set_coil(10, true);

    // 読み出しRTUリクエストを作成
    uint8_t req_pdu[5];
    size_t req_pdu_len = 0;
    modbus_master_req_read_holding_registers(100, 1, req_pdu, &req_pdu_len);

    uint8_t req_adu[MODBUS_MAX_ADU_LENGTH];
    size_t req_adu_len = 0;
    modbus_rtu_encode_frame(1, req_pdu, req_pdu_len, req_adu, &req_adu_len);

    std::vector<uint8_t> req_vec(req_adu, req_adu + req_adu_len);
    std::vector<uint8_t> resp_vec;

    bool handled = slave.handle_rtu_request(req_vec, resp_vec);
    CHECK(handled);
    CHECK(!resp_vec.empty());

    // 応答パース
    uint8_t resp_unit_id = 0;
    const uint8_t *resp_pdu = nullptr;
    size_t resp_pdu_len = 0;
    modbus_status_t st = modbus_rtu_decode_frame(resp_vec.data(), resp_vec.size(), &resp_unit_id,
                                                 &resp_pdu, &resp_pdu_len);
    CHECK(st == MODBUS_OK);
    CHECK(resp_unit_id == 1);

    uint16_t reg_val = 0;
    st = modbus_master_parse_read_registers_resp(resp_pdu, resp_pdu_len, &reg_val, 1);
    CHECK(st == MODBUS_OK);
    CHECK(reg_val == 12345);

    std::cout << "# [Test] C++ Slave Wrapper passed!" << std::endl;
}


// ---- 回帰テスト: master 応答デコードの境界 (レビュー指摘) ----
TEST_CASE(regression_master_parse_rejects_truncated_counts) {
    // expected_count=129 は uint8_t 切り詰めで期待バイト数 2 と誤判定されていた
    std::vector<uint8_t> resp = {0x03, 0x02, 0x12, 0x34};
    std::vector<uint16_t> dest(200, 0);
    CHECK(modbus_master_parse_read_registers_resp(resp.data(), resp.size(), dest.data(), 129) ==
          MODBUS_ERR_INVALID_PARAM);
    CHECK(modbus_master_parse_read_registers_resp(resp.data(), resp.size(), dest.data(), 0) ==
          MODBUS_ERR_INVALID_PARAM);
    CHECK(modbus_master_parse_read_registers_resp(resp.data(), resp.size(), dest.data(), 1) ==
          MODBUS_OK);
    CHECK(dest[0] == 0x1234);

    // 2048 coils -> 256 bytes は uint8_t で 0 に切り詰められていた
    std::vector<uint8_t> coil_resp = {0x01, 0x00};
    std::vector<uint8_t> coils(300, 0);
    CHECK(modbus_master_parse_read_coils_resp(coil_resp.data(), coil_resp.size(), coils.data(), 2048) ==
          MODBUS_ERR_INVALID_PARAM);

    // byte_count が実データより長い (切り詰め応答)
    std::vector<uint8_t> shortresp = {0x03, 0x04, 0x00, 0x01};
    CHECK(modbus_master_parse_read_registers_resp(shortresp.data(), shortresp.size(), dest.data(), 2) ==
          MODBUS_ERR_INVALID_RESPONSE);
    std::vector<uint8_t> coil_ok = {0x01, 0x01, 0x05};
    CHECK(modbus_master_parse_read_coils_resp(coil_ok.data(), coil_ok.size(), coils.data(), 8) == MODBUS_OK);
    CHECK(coils[0] == 0x05);
}

EMBED_TEST_MAIN()
