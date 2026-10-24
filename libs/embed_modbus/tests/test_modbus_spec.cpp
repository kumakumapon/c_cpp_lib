// Modbus の仕様を押さえるテスト。
//
// 期待値は MODBUS Application Protocol Specification V1.1b3 (6章のファンクション例、7章の
// 例外応答)と MODBUS over Serial Line Specification V1.02 (RTUフレーム、CRC、LRC)に載っている
// バイト列である。ライブラリ自身のエンコーダとデコーダを往復させるのではなく、
// 「規格のバイト列がそのまま得られること」「規格のバイト列をそのまま解釈できること」を確認する。
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "embed_modbus/modbus_crc.h"
#include "embed_modbus/modbus_master.h"
#include "embed_modbus/modbus_rtu.h"
#include "embed_modbus/modbus_slave.h"
#include "embed_modbus/modbus_types.h"
#include "embed_test/embed_test.hpp"

using Bytes = std::vector<uint8_t>;

static bool check_bytes(const uint8_t *actual, size_t n, const Bytes &expected, const char *expr,
                        const char *file, int line) {
    bool ok = n == expected.size() && (n == 0 || std::memcmp(actual, expected.data(), n) == 0);
    if (!::embed_test::check(ok, expr, file, line)) {
        std::string a, e;
        char b[4];
        for (size_t i = 0; i < n; ++i) {
            std::snprintf(b, sizeof b, "%02X ", actual[i]);
            a += b;
        }
        for (uint8_t x : expected) {
            std::snprintf(b, sizeof b, "%02X ", x);
            e += b;
        }
        std::printf("# expected: %s\n# actual:   %s\n", e.c_str(), a.c_str());
    }
    return ok;
}
#define CHECK_BYTES(ptr, len, ...) check_bytes((ptr), (len), Bytes{__VA_ARGS__}, #ptr, __FILE__, __LINE__)

// ---- 仮想デバイス -----------------------------------------------------------
// コイルは「要求した先頭のコイルが1バイト目の最下位ビット」(仕様 6.1)の順で詰める。
namespace {
constexpr unsigned kSize = 4096;

struct Device {
    bool coil[kSize] = {};
    bool discrete[kSize] = {};
    uint16_t holding[kSize] = {};
    uint16_t input[kSize] = {};
    modbus_exception_t force = MODBUS_EX_NONE; // NONE 以外ならコールバックがこの例外を返す
    unsigned calls = 0;                        // コールバックが呼ばれた回数
};

Device *dev(void *u) { return static_cast<Device *>(u); }

modbus_exception_t pack_bits(const bool *src, uint16_t a, uint16_t c, uint8_t *d) {
    if (a + c > kSize)
        return MODBUS_EX_ILLEGAL_DATA_ADDRESS;
    for (uint16_t i = 0; i < c; ++i)
        if (src[a + i])
            d[i / 8] |= (uint8_t)(1u << (i % 8));
    return MODBUS_EX_NONE;
}
modbus_exception_t rd_coils(uint16_t a, uint16_t c, uint8_t *d, void *u) {
    ++dev(u)->calls;
    return dev(u)->force != MODBUS_EX_NONE ? dev(u)->force : pack_bits(dev(u)->coil, a, c, d);
}
modbus_exception_t rd_discrete(uint16_t a, uint16_t c, uint8_t *d, void *u) {
    ++dev(u)->calls;
    return dev(u)->force != MODBUS_EX_NONE ? dev(u)->force : pack_bits(dev(u)->discrete, a, c, d);
}
modbus_exception_t rd_regs(const uint16_t *src, uint16_t a, uint16_t c, uint16_t *d) {
    if (a + c > kSize)
        return MODBUS_EX_ILLEGAL_DATA_ADDRESS;
    for (uint16_t i = 0; i < c; ++i)
        d[i] = src[a + i];
    return MODBUS_EX_NONE;
}
modbus_exception_t rd_holding(uint16_t a, uint16_t c, uint16_t *d, void *u) {
    ++dev(u)->calls;
    return dev(u)->force != MODBUS_EX_NONE ? dev(u)->force : rd_regs(dev(u)->holding, a, c, d);
}
modbus_exception_t rd_input(uint16_t a, uint16_t c, uint16_t *d, void *u) {
    ++dev(u)->calls;
    return dev(u)->force != MODBUS_EX_NONE ? dev(u)->force : rd_regs(dev(u)->input, a, c, d);
}
modbus_exception_t wr_coils(uint16_t a, uint16_t c, const uint8_t *s, void *u) {
    ++dev(u)->calls;
    if (dev(u)->force != MODBUS_EX_NONE)
        return dev(u)->force;
    if (a + c > kSize)
        return MODBUS_EX_ILLEGAL_DATA_ADDRESS;
    for (uint16_t i = 0; i < c; ++i)
        dev(u)->coil[a + i] = (s[i / 8] >> (i % 8)) & 1;
    return MODBUS_EX_NONE;
}
modbus_exception_t wr_holding(uint16_t a, uint16_t c, const uint16_t *s, void *u) {
    ++dev(u)->calls;
    if (dev(u)->force != MODBUS_EX_NONE)
        return dev(u)->force;
    if (a + c > kSize)
        return MODBUS_EX_ILLEGAL_DATA_ADDRESS;
    for (uint16_t i = 0; i < c; ++i)
        dev(u)->holding[a + i] = s[i];
    return MODBUS_EX_NONE;
}

void init_slave(modbus_slave_t &slave, Device &d, uint8_t unit = 1) {
    modbus_slave_callbacks_t cbs{};
    cbs.read_coils = rd_coils;
    cbs.read_discrete_inputs = rd_discrete;
    cbs.read_holding_registers = rd_holding;
    cbs.read_input_registers = rd_input;
    cbs.write_coils = wr_coils;
    cbs.write_holding_registers = wr_holding;
    modbus_slave_init(&slave, unit, &cbs, &d);
}

// 要求PDUをスレーブへ渡し、応答PDUを返す
Bytes transact(const modbus_slave_t &slave, const Bytes &req) {
    uint8_t resp[MODBUS_MAX_PDU_LENGTH + 8] = {};
    size_t n = 0;
    bool responded = modbus_slave_process_pdu(&slave, req.data(), req.size(), resp, &n);
    CHECK(responded);
    return Bytes(resp, resp + n);
}

void set_bits(bool *dst, unsigned start, unsigned count, const Bytes &packed) {
    for (unsigned i = 0; i < count; ++i)
        dst[start + i] = (packed[i / 8] >> (i % 8)) & 1;
}
} // namespace

#define CHECK_PDU(actual, ...) CHECK_BYTES((actual).data(), (actual).size(), __VA_ARGS__)

// =============================================================================
// 仕様 6.1〜6.4: 読み出し系ファンクションの要求・応答の例
// =============================================================================
TEST_CASE(spec_6_read_functions_request_and_response_bytes) {
    Device d;
    modbus_slave_t slave;
    init_slave(slave, d);
    uint8_t pdu[MODBUS_MAX_PDU_LENGTH];
    size_t n = 0;

    // 6.1 Read Coils: コイル 20〜38 (アドレス 19, 19個) -> CD 6B 05
    set_bits(d.coil, 19, 19, {0xCD, 0x6B, 0x05});
    REQUIRE(modbus_master_req_read_coils(19, 19, pdu, &n));
    CHECK_BYTES(pdu, n, 0x01, 0x00, 0x13, 0x00, 0x13);
    Bytes resp = transact(slave, Bytes(pdu, pdu + n));
    CHECK_PDU(resp, 0x01, 0x03, 0xCD, 0x6B, 0x05);
    uint8_t coils[3] = {};
    CHECK_EQ(modbus_master_parse_read_coils_resp(resp.data(), resp.size(), coils, 19), MODBUS_OK);
    CHECK_BYTES(coils, 3, 0xCD, 0x6B, 0x05);

    // 6.2 Read Discrete Inputs: 入力 197〜218 (アドレス 196, 22個) -> AC DB 35
    set_bits(d.discrete, 196, 22, {0xAC, 0xDB, 0x35});
    REQUIRE(modbus_master_req_read_discrete_inputs(196, 22, pdu, &n));
    CHECK_BYTES(pdu, n, 0x02, 0x00, 0xC4, 0x00, 0x16);
    resp = transact(slave, Bytes(pdu, pdu + n));
    CHECK_PDU(resp, 0x02, 0x03, 0xAC, 0xDB, 0x35);
    CHECK_EQ(modbus_master_parse_read_coils_resp(resp.data(), resp.size(), coils, 22), MODBUS_OK);
    CHECK_BYTES(coils, 3, 0xAC, 0xDB, 0x35);

    // 6.3 Read Holding Registers: レジスタ 108〜110 (アドレス 107, 3個) -> 022B 0000 0064
    d.holding[107] = 0x022B;
    d.holding[108] = 0x0000;
    d.holding[109] = 0x0064;
    REQUIRE(modbus_master_req_read_holding_registers(107, 3, pdu, &n));
    CHECK_BYTES(pdu, n, 0x03, 0x00, 0x6B, 0x00, 0x03);
    resp = transact(slave, Bytes(pdu, pdu + n));
    CHECK_PDU(resp, 0x03, 0x06, 0x02, 0x2B, 0x00, 0x00, 0x00, 0x64);
    uint16_t regs[3] = {};
    CHECK_EQ(modbus_master_parse_read_registers_resp(resp.data(), resp.size(), regs, 3), MODBUS_OK);
    CHECK_EQ(regs[0], 555u);
    CHECK_EQ(regs[1], 0u);
    CHECK_EQ(regs[2], 100u);

    // 6.4 Read Input Registers: 入力レジスタ 9 (アドレス 8, 1個) -> 000A
    d.input[8] = 0x000A;
    REQUIRE(modbus_master_req_read_input_registers(8, 1, pdu, &n));
    CHECK_BYTES(pdu, n, 0x04, 0x00, 0x08, 0x00, 0x01);
    resp = transact(slave, Bytes(pdu, pdu + n));
    CHECK_PDU(resp, 0x04, 0x02, 0x00, 0x0A);
    CHECK_EQ(modbus_master_parse_read_registers_resp(resp.data(), resp.size(), regs, 1), MODBUS_OK);
    CHECK_EQ(regs[0], 10u);
}

// =============================================================================
// 仕様 6.5, 6.6, 6.11, 6.12: 書き込み系ファンクションの要求・応答の例
// =============================================================================
TEST_CASE(spec_6_write_functions_request_and_response_bytes) {
    Device d;
    modbus_slave_t slave;
    init_slave(slave, d);
    uint8_t pdu[MODBUS_MAX_PDU_LENGTH];
    size_t n = 0;

    // 6.5 Write Single Coil: コイル 173 (アドレス 172) を ON -> 応答は要求のエコー
    REQUIRE(modbus_master_req_write_single_coil(172, true, pdu, &n));
    CHECK_BYTES(pdu, n, 0x05, 0x00, 0xAC, 0xFF, 0x00);
    Bytes resp = transact(slave, Bytes(pdu, pdu + n));
    CHECK_PDU(resp, 0x05, 0x00, 0xAC, 0xFF, 0x00);
    CHECK(d.coil[172]);
    CHECK_EQ(modbus_master_parse_write_resp(resp.data(), resp.size(), 0x05, 172), MODBUS_OK);
    // OFF は 00 00
    REQUIRE(modbus_master_req_write_single_coil(172, false, pdu, &n));
    CHECK_BYTES(pdu, n, 0x05, 0x00, 0xAC, 0x00, 0x00);
    resp = transact(slave, Bytes(pdu, pdu + n));
    CHECK_PDU(resp, 0x05, 0x00, 0xAC, 0x00, 0x00);
    CHECK(!d.coil[172]);

    // 6.6 Write Single Register: レジスタ 2 (アドレス 1) に 0003 -> エコー
    REQUIRE(modbus_master_req_write_single_register(1, 3, pdu, &n));
    CHECK_BYTES(pdu, n, 0x06, 0x00, 0x01, 0x00, 0x03);
    resp = transact(slave, Bytes(pdu, pdu + n));
    CHECK_PDU(resp, 0x06, 0x00, 0x01, 0x00, 0x03);
    CHECK_EQ(d.holding[1], 3u);
    CHECK_EQ(modbus_master_parse_write_resp(resp.data(), resp.size(), 0x06, 1), MODBUS_OK);

    // 6.11 Write Multiple Coils: コイル 20〜29 (アドレス 19, 10個) に CD 01 -> 応答は先頭アドレスと個数
    const uint8_t packed[] = {0xCD, 0x01};
    REQUIRE(modbus_master_req_write_multiple_coils(19, 10, packed, pdu, &n));
    CHECK_BYTES(pdu, n, 0x0F, 0x00, 0x13, 0x00, 0x0A, 0x02, 0xCD, 0x01);
    resp = transact(slave, Bytes(pdu, pdu + n));
    CHECK_PDU(resp, 0x0F, 0x00, 0x13, 0x00, 0x0A);
    // CD = 1,0,1,1,0,0,1,1 (最下位ビットが先頭のコイル)、01 = 1,0
    const bool expected_coils[10] = {1, 0, 1, 1, 0, 0, 1, 1, 1, 0};
    for (unsigned i = 0; i < 10; ++i)
        CHECK_EQ(d.coil[19 + i], expected_coils[i]);
    CHECK(!d.coil[29]); // 範囲外は変更されない
    CHECK_EQ(modbus_master_parse_write_resp(resp.data(), resp.size(), 0x0F, 19), MODBUS_OK);

    // 6.12 Write Multiple Registers: レジスタ 2〜3 (アドレス 1, 2個) に 000A 0102
    const uint16_t values[] = {0x000A, 0x0102};
    REQUIRE(modbus_master_req_write_multiple_registers(1, 2, values, pdu, &n));
    CHECK_BYTES(pdu, n, 0x10, 0x00, 0x01, 0x00, 0x02, 0x04, 0x00, 0x0A, 0x01, 0x02);
    resp = transact(slave, Bytes(pdu, pdu + n));
    CHECK_PDU(resp, 0x10, 0x00, 0x01, 0x00, 0x02);
    CHECK_EQ(d.holding[1], 0x000Au);
    CHECK_EQ(d.holding[2], 0x0102u);
    CHECK_EQ(modbus_master_parse_write_resp(resp.data(), resp.size(), 0x10, 1), MODBUS_OK);
}

// =============================================================================
// 仕様 7 と各ファンクションの節: 例外応答 (ファンクションコード | 0x80, 例外コード)
// =============================================================================
TEST_CASE(spec_exception_for_address_out_of_range_is_02) {
    Device d;
    modbus_slave_t slave;
    init_slave(slave, d);
    // 仮想デバイスは 0..4095。アドレス 4095 から 2 個は範囲外
    CHECK_PDU(transact(slave, {0x01, 0x0F, 0xFF, 0x00, 0x02}), 0x81, 0x02);
    CHECK_PDU(transact(slave, {0x02, 0x0F, 0xFF, 0x00, 0x02}), 0x82, 0x02);
    CHECK_PDU(transact(slave, {0x03, 0x0F, 0xFF, 0x00, 0x02}), 0x83, 0x02);
    CHECK_PDU(transact(slave, {0x04, 0x0F, 0xFF, 0x00, 0x02}), 0x84, 0x02);
    CHECK_PDU(transact(slave, {0x05, 0x10, 0x00, 0xFF, 0x00}), 0x85, 0x02);
    CHECK_PDU(transact(slave, {0x06, 0x10, 0x00, 0x00, 0x01}), 0x86, 0x02);
    CHECK_PDU(transact(slave, {0x0F, 0x0F, 0xFF, 0x00, 0x02, 0x01, 0x03}), 0x8F, 0x02);
    CHECK_PDU(transact(slave, {0x10, 0x0F, 0xFF, 0x00, 0x02, 0x04, 0, 1, 0, 2}), 0x90, 0x02);
    // 範囲外への書き込みは何も変更しない
    CHECK(!d.coil[4095]);
    CHECK_EQ(d.holding[4095], 0u);
}

TEST_CASE(spec_exception_for_quantity_limits_is_03) {
    Device d;
    modbus_slave_t slave;
    init_slave(slave, d);

    // 読み出しの個数: コイル/離散入力は 1..2000 (0x07D0)、レジスタは 1..125 (0x7D)
    for (int fc_i : {0x01, 0x02}) {
        const uint8_t fc = static_cast<uint8_t>(fc_i);
        CHECK_PDU(transact(slave, {fc, 0x00, 0x00, 0x00, 0x00}), (uint8_t)(fc | 0x80), 0x03); // 0個
        CHECK_PDU(transact(slave, {fc, 0x00, 0x00, 0x07, 0xD1}), (uint8_t)(fc | 0x80), 0x03); // 2001個
        Bytes ok = transact(slave, {fc, 0x00, 0x00, 0x07, 0xD0});                              // 2000個
        CHECK_EQ(ok.size(), 2u + 250u);
        CHECK_EQ(ok[0], fc);
        CHECK_EQ(ok[1], 250u);
        CHECK_PDU(transact(slave, {fc, 0x00, 0x00, 0x00, 0x01}) , fc, 0x01, 0x00); // 1個
    }
    for (int fc_i : {0x03, 0x04}) {
        const uint8_t fc = static_cast<uint8_t>(fc_i);
        CHECK_PDU(transact(slave, {fc, 0x00, 0x00, 0x00, 0x00}), (uint8_t)(fc | 0x80), 0x03); // 0個
        CHECK_PDU(transact(slave, {fc, 0x00, 0x00, 0x00, 0x7E}), (uint8_t)(fc | 0x80), 0x03); // 126個
        Bytes ok = transact(slave, {fc, 0x00, 0x00, 0x00, 0x7D});                              // 125個
        CHECK_EQ(ok.size(), 2u + 250u);
        CHECK_EQ(ok[1], 250u);
    }

    // Write Multiple Coils: 1..1968 (0x07B0)、バイト数 = ceil(個数/8)
    {
        Bytes req = {0x0F, 0x00, 0x00, 0x07, 0xB0, 246};
        req.resize(6 + 246, 0xFF);
        CHECK_PDU(transact(slave, req), 0x0F, 0x00, 0x00, 0x07, 0xB0); // 1968個は受理
        Bytes over = {0x0F, 0x00, 0x00, 0x07, 0xB1, 247};
        over.resize(6 + 247, 0xFF);
        CHECK_PDU(transact(slave, over), 0x8F, 0x03); // 1969個
        CHECK_PDU(transact(slave, {0x0F, 0x00, 0x00, 0x00, 0x00, 0x00}), 0x8F, 0x03); // 0個
        CHECK_PDU(transact(slave, {0x0F, 0x00, 0x13, 0x00, 0x0A, 0x03, 0xCD, 0x01, 0x00}), 0x8F, 0x03); // バイト数不一致
        CHECK_PDU(transact(slave, {0x0F, 0x00, 0x13, 0x00, 0x0A, 0x02, 0xCD}), 0x8F, 0x03); // データ不足
    }
    // Write Multiple Registers: 1..123 (0x7B)、バイト数 = 個数 x 2
    {
        Bytes req = {0x10, 0x00, 0x00, 0x00, 0x7B, 246};
        req.resize(6 + 246, 0x00);
        CHECK_PDU(transact(slave, req), 0x10, 0x00, 0x00, 0x00, 0x7B); // 123個は受理
        Bytes over = {0x10, 0x00, 0x00, 0x00, 0x7C, 248};
        over.resize(6 + 248, 0x00);
        CHECK_PDU(transact(slave, over), 0x90, 0x03); // 124個
        CHECK_PDU(transact(slave, {0x10, 0x00, 0x00, 0x00, 0x00, 0x00}), 0x90, 0x03); // 0個
        CHECK_PDU(transact(slave, {0x10, 0x00, 0x01, 0x00, 0x02, 0x03, 0, 1, 0, 2}), 0x90, 0x03); // バイト数不一致
        CHECK_PDU(transact(slave, {0x10, 0x00, 0x01, 0x00, 0x02, 0x04, 0, 1}), 0x90, 0x03); // データ不足
    }
}

TEST_CASE(spec_write_single_coil_accepts_only_FF00_and_0000) {
    Device d;
    modbus_slave_t slave;
    init_slave(slave, d);
    // 6.5: 0xFF00 = ON, 0x0000 = OFF。それ以外の値は例外 03
    for (Bytes bad : {Bytes{0x05, 0x00, 0x01, 0x00, 0x01}, Bytes{0x05, 0x00, 0x01, 0xFF, 0x01},
                      Bytes{0x05, 0x00, 0x01, 0x00, 0xFF}, Bytes{0x05, 0x00, 0x01, 0xFF, 0xFF},
                      Bytes{0x05, 0x00, 0x01, 0x01, 0x00}}) {
        CHECK_PDU(transact(slave, bad), 0x85, 0x03);
    }
    CHECK(!d.coil[1]);
    CHECK_PDU(transact(slave, {0x05, 0x00, 0x01, 0xFF, 0x00}), 0x05, 0x00, 0x01, 0xFF, 0x00);
    CHECK(d.coil[1]);
    CHECK_PDU(transact(slave, {0x05, 0x00, 0x01, 0x00, 0x00}), 0x05, 0x00, 0x01, 0x00, 0x00);
    CHECK(!d.coil[1]);
}

TEST_CASE(spec_exception_for_unsupported_function_is_01) {
    Device d;
    modbus_slave_t slave;
    init_slave(slave, d);
    // 未対応のファンクションコードは (fc | 0x80, 0x01)。コールバックは呼ばれない
    for (int fc_i : {0x00, 0x07, 0x08, 0x0B, 0x0C, 0x11, 0x14, 0x15, 0x16, 0x17, 0x18, 0x2B, 0x7F}) {
        const uint8_t fc = static_cast<uint8_t>(fc_i);
        CHECK_PDU(transact(slave, {fc, 0x00, 0x00, 0x00, 0x01}), (uint8_t)(fc | 0x80), 0x01);
    }
    CHECK_EQ(d.calls, 0u);

    // 対応するコールバックが無いファンクションも 01
    modbus_slave_t bare;
    modbus_slave_init(&bare, 1, nullptr, nullptr);
    for (Bytes req : {Bytes{0x01, 0, 0, 0, 1}, Bytes{0x02, 0, 0, 0, 1}, Bytes{0x03, 0, 0, 0, 1},
                      Bytes{0x04, 0, 0, 0, 1}, Bytes{0x05, 0, 0, 0xFF, 0}, Bytes{0x06, 0, 0, 0, 1},
                      Bytes{0x0F, 0, 0, 0, 1, 1, 1}, Bytes{0x10, 0, 0, 0, 1, 2, 0, 1}}) {
        CHECK_PDU(transact(bare, req), (uint8_t)(req[0] | 0x80), 0x01);
    }
}

TEST_CASE(spec_callback_exceptions_are_returned_to_the_master) {
    Device d;
    modbus_slave_t slave;
    init_slave(slave, d);
    // 7章の例外コード: 01〜06, 08, 0A, 0B
    for (modbus_exception_t ex : {MODBUS_EX_ILLEGAL_FUNCTION, MODBUS_EX_ILLEGAL_DATA_ADDRESS,
                                  MODBUS_EX_ILLEGAL_DATA_VALUE, MODBUS_EX_SERVER_DEVICE_FAILURE,
                                  MODBUS_EX_ACKNOWLEDGE, MODBUS_EX_SERVER_DEVICE_BUSY,
                                  MODBUS_EX_MEMORY_PARITY_ERROR, MODBUS_EX_GATEWAY_PATH_UNAVAILABLE,
                                  MODBUS_EX_GATEWAY_TARGET_FAILED}) {
        d.force = ex;
        CHECK_PDU(transact(slave, {0x03, 0x00, 0x00, 0x00, 0x01}), 0x83, (uint8_t)ex);
        CHECK_PDU(transact(slave, {0x06, 0x00, 0x00, 0x00, 0x01}), 0x86, (uint8_t)ex);
    }
    CHECK_EQ(MODBUS_EX_ILLEGAL_FUNCTION, 0x01);
    CHECK_EQ(MODBUS_EX_ILLEGAL_DATA_ADDRESS, 0x02);
    CHECK_EQ(MODBUS_EX_ILLEGAL_DATA_VALUE, 0x03);
    CHECK_EQ(MODBUS_EX_SERVER_DEVICE_FAILURE, 0x04);
    CHECK_EQ(MODBUS_EX_ACKNOWLEDGE, 0x05);
    CHECK_EQ(MODBUS_EX_SERVER_DEVICE_BUSY, 0x06);
    CHECK_EQ(MODBUS_EX_MEMORY_PARITY_ERROR, 0x08);
    CHECK_EQ(MODBUS_EX_GATEWAY_PATH_UNAVAILABLE, 0x0A);
    CHECK_EQ(MODBUS_EX_GATEWAY_TARGET_FAILED, 0x0B);
}

TEST_CASE(spec_truncated_requests_are_rejected_with_03) {
    Device d;
    modbus_slave_t slave;
    init_slave(slave, d);
    // 要求が短い場合はデータ値不正 (03)。コールバックは呼ばれず、デバイスは変化しない
    for (int fc_i : {0x01, 0x02, 0x03, 0x04, 0x05, 0x06}) {
        const uint8_t fc = static_cast<uint8_t>(fc_i);
        CHECK_PDU(transact(slave, {fc}), (uint8_t)(fc | 0x80), 0x03);
        CHECK_PDU(transact(slave, {fc, 0x00, 0x00, 0x00}), (uint8_t)(fc | 0x80), 0x03);
    }
    CHECK_PDU(transact(slave, {0x0F, 0x00, 0x00, 0x00, 0x01}), 0x8F, 0x03);
    CHECK_PDU(transact(slave, {0x10, 0x00, 0x00, 0x00, 0x01}), 0x90, 0x03);
    CHECK_EQ(d.calls, 0u);
}

// =============================================================================
// CRC-16 / LRC (MODBUS over Serial Line V1.02)
// =============================================================================
TEST_CASE(spec_serial_line_crc16_and_lrc_vectors) {
    // CRC は 0xFFFF 初期値、多項式 0xA001(反転)。広く公開されている例
    const uint8_t a[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x0A};
    CHECK_EQ(modbus_crc16(a, sizeof a), 0xCDC5u); // 送出順は C5 CD
    const uint8_t b[] = {0x11, 0x03, 0x00, 0x6B, 0x00, 0x03};
    CHECK_EQ(modbus_crc16(b, sizeof b), 0x8776u); // 送出順は 76 87
    const uint8_t c[] = {0x11, 0x03, 0x06, 0xAE, 0x41, 0x56, 0x52, 0x43, 0x40};
    CHECK_EQ(modbus_crc16(c, sizeof c), 0xAD49u); // 送出順は 49 AD
    const uint8_t d[] = {0x01, 0x04, 0x02, 0xFF, 0xFF};
    CHECK_EQ(modbus_crc16(d, sizeof d), 0x80B8u); // 送出順は B8 80
    const uint8_t digits[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    CHECK_EQ(modbus_crc16(digits, sizeof digits), 0x4B37u); // CRC-16/MODBUS のチェック値
    // フレーム全体(CRC を含む)の CRC は 0
    const uint8_t whole[] = {0x11, 0x03, 0x00, 0x6B, 0x00, 0x03, 0x76, 0x87};
    CHECK_EQ(modbus_crc16(whole, sizeof whole), 0u);

    // LRC は全バイトの和の2の補数 (ASCII モード)
    CHECK_EQ(modbus_lrc(a, sizeof a), 0xF2u);
    CHECK_EQ(modbus_lrc(b, sizeof b), 0x7Eu);
    const uint8_t wrap[] = {0xFF, 0x02}; // 和が 0x101 -> 下位8bit 0x01 -> 補数 0xFF
    CHECK_EQ(modbus_lrc(wrap, sizeof wrap), 0xFFu);
}

// =============================================================================
// RTU フレーム (MODBUS over Serial Line V1.02): アドレス, PDU, CRC(下位バイトが先)
// =============================================================================
TEST_CASE(spec_rtu_frame_encoding_and_decoding) {
    uint8_t adu[MODBUS_MAX_ADU_LENGTH + 8];
    size_t n = 0;
    const uint8_t pdu[] = {0x03, 0x00, 0x6B, 0x00, 0x03};
    REQUIRE(modbus_rtu_encode_frame(0x11, pdu, sizeof pdu, adu, &n));
    CHECK_BYTES(adu, n, 0x11, 0x03, 0x00, 0x6B, 0x00, 0x03, 0x76, 0x87);

    uint8_t unit = 0;
    const uint8_t *p = nullptr;
    size_t pl = 0;
    CHECK_EQ(modbus_rtu_decode_frame(adu, n, &unit, &p, &pl), MODBUS_OK);
    CHECK_EQ(unit, 0x11u);
    CHECK_BYTES(p, pl, 0x03, 0x00, 0x6B, 0x00, 0x03);

    // CRC の1ビットでも違えば拒否
    for (size_t i = 0; i < n; ++i) {
        uint8_t bad[16];
        std::memcpy(bad, adu, n);
        bad[i] ^= 0x01;
        CHECK_EQ(modbus_rtu_decode_frame(bad, n, &unit, &p, &pl), MODBUS_ERR_CRC);
    }
    // 最小フレームは アドレス + FC + CRC(2) = 4 バイト
    CHECK_EQ(modbus_rtu_decode_frame(adu, 3, &unit, &p, &pl), MODBUS_ERR_INVALID_PARAM);
    const uint8_t minimal[] = {0x01, 0x07, 0x00, 0x00}; // CRC は後で付け替える
    REQUIRE(modbus_rtu_encode_frame(0x01, minimal + 1, 1, adu, &n));
    CHECK_EQ(n, 4u);
    CHECK_EQ(modbus_rtu_decode_frame(adu, n, &unit, &p, &pl), MODBUS_OK);
    CHECK_EQ(pl, 1u);

    // ADU の最大長は 256 バイト (PDU 最大 253)
    Bytes max_pdu(253, 0x5A);
    REQUIRE(modbus_rtu_encode_frame(0x01, max_pdu.data(), max_pdu.size(), adu, &n));
    CHECK_EQ(n, 256u);
    Bytes too_long(254, 0x5A);
    CHECK(!modbus_rtu_encode_frame(0x01, too_long.data(), too_long.size(), adu, &n));
    CHECK(!modbus_rtu_encode_frame(0x01, pdu, 0, adu, &n)); // PDU なしは不可
}

TEST_CASE(spec_rtu_slave_addressing_and_responses) {
    Device d;
    modbus_slave_t slave;
    init_slave(slave, d, 0x11);
    d.holding[107] = 0xAE41;
    d.holding[108] = 0x5652;
    d.holding[109] = 0x4340;
    uint8_t resp[MODBUS_MAX_ADU_LENGTH];
    size_t rl = 0;

    // 自局宛て: 公開例のフレームそのもの (要求 11 03 006B 0003 7687 -> 応答 ... 49AD)
    const uint8_t req[] = {0x11, 0x03, 0x00, 0x6B, 0x00, 0x03, 0x76, 0x87};
    REQUIRE(modbus_rtu_slave_handle(&slave, req, sizeof req, resp, &rl));
    CHECK_BYTES(resp, rl, 0x11, 0x03, 0x06, 0xAE, 0x41, 0x56, 0x52, 0x43, 0x40, 0x49, 0xAD);

    // 例外応答もアドレス + (FC|0x80) + 例外コード + CRC のフレームになる
    uint8_t bad_req[16];
    size_t bl = 0;
    const uint8_t bad_pdu[] = {0x03, 0x0F, 0xFF, 0x00, 0x02};
    REQUIRE(modbus_rtu_encode_frame(0x11, bad_pdu, sizeof bad_pdu, bad_req, &bl));
    REQUIRE(modbus_rtu_slave_handle(&slave, bad_req, bl, resp, &rl));
    CHECK_EQ(rl, 5u);
    CHECK_BYTES(resp, 3, 0x11, 0x83, 0x02);
    CHECK_EQ(modbus_crc16(resp, rl), 0u); // 末尾の CRC が正しい

    // 他局宛ては無視する(応答なし、コールバックも呼ばない)
    unsigned calls = d.calls;
    uint8_t other[16];
    size_t ol = 0;
    const uint8_t read_pdu[] = {0x03, 0x00, 0x6B, 0x00, 0x03};
    REQUIRE(modbus_rtu_encode_frame(0x12, read_pdu, sizeof read_pdu, other, &ol));
    CHECK(!modbus_rtu_slave_handle(&slave, other, ol, resp, &rl));
    CHECK_EQ(d.calls, calls);

    // CRC 不良は無視する
    uint8_t corrupt[sizeof req];
    std::memcpy(corrupt, req, sizeof req);
    corrupt[sizeof req - 1] ^= 0xFF;
    CHECK(!modbus_rtu_slave_handle(&slave, corrupt, sizeof corrupt, resp, &rl));
    CHECK_EQ(d.calls, calls);

    // ブロードキャスト(アドレス 0): 書き込みは全局で実行されるが、応答は返さない
    const uint8_t write_pdu[] = {0x06, 0x00, 0x01, 0x12, 0x34};
    REQUIRE(modbus_rtu_encode_frame(0x00, write_pdu, sizeof write_pdu, other, &ol));
    CHECK(!modbus_rtu_slave_handle(&slave, other, ol, resp, &rl));
    CHECK_EQ(d.holding[1], 0x1234u);
}

// =============================================================================
// マスタ側の応答デコード
// =============================================================================
TEST_CASE(spec_master_recognizes_exception_responses) {
    uint16_t regs[4] = {};
    uint8_t bits[8] = {};
    // 例外応答は 2 バイト (FC|0x80, 例外コード)。どの種類の応答でも例外として報告される
    const uint8_t ex_read[] = {0x83, 0x02};
    CHECK_EQ(modbus_master_parse_read_registers_resp(ex_read, sizeof ex_read, regs, 3),
             MODBUS_ERR_EXCEPTION_RECEIVED);
    const uint8_t ex_coil[] = {0x81, 0x02};
    CHECK_EQ(modbus_master_parse_read_coils_resp(ex_coil, sizeof ex_coil, bits, 8),
             MODBUS_ERR_EXCEPTION_RECEIVED);
    for (int fc_i : {0x05, 0x06, 0x0F, 0x10}) {
        const uint8_t fc = static_cast<uint8_t>(fc_i);
        const uint8_t ex_write[] = {(uint8_t)(fc | 0x80), 0x02};
        CHECK_EQ(modbus_master_parse_write_resp(ex_write, sizeof ex_write, fc, 0),
                 MODBUS_ERR_EXCEPTION_RECEIVED);
    }
}

TEST_CASE(spec_master_validates_response_contents) {
    uint16_t regs[4] = {};
    uint8_t bits[8] = {};
    // Read Holding Registers: バイト数は個数 x 2 と一致しなければならない
    const uint8_t r3[] = {0x03, 0x06, 0x02, 0x2B, 0x00, 0x00, 0x00, 0x64};
    CHECK_EQ(modbus_master_parse_read_registers_resp(r3, sizeof r3, regs, 3), MODBUS_OK);
    CHECK_EQ(regs[0], 0x022Bu);
    CHECK_EQ(regs[2], 0x0064u);
    CHECK_EQ(modbus_master_parse_read_registers_resp(r3, sizeof r3, regs, 2), MODBUS_ERR_INVALID_RESPONSE);
    CHECK_EQ(modbus_master_parse_read_registers_resp(r3, sizeof r3, regs, 4), MODBUS_ERR_INVALID_RESPONSE);
    CHECK_EQ(modbus_master_parse_read_registers_resp(r3, sizeof r3 - 1, regs, 3), MODBUS_ERR_INVALID_RESPONSE);

    // Read Coils: バイト数 = ceil(個数 / 8)
    const uint8_t c19[] = {0x01, 0x03, 0xCD, 0x6B, 0x05};
    CHECK_EQ(modbus_master_parse_read_coils_resp(c19, sizeof c19, bits, 19), MODBUS_OK);
    CHECK_EQ(bits[0], 0xCDu);
    CHECK_EQ(bits[2], 0x05u);
    CHECK_EQ(modbus_master_parse_read_coils_resp(c19, sizeof c19, bits, 24), MODBUS_OK); // 24個でも3バイト
    CHECK_EQ(modbus_master_parse_read_coils_resp(c19, sizeof c19, bits, 25), MODBUS_ERR_INVALID_RESPONSE);
    CHECK_EQ(modbus_master_parse_read_coils_resp(c19, sizeof c19, bits, 16), MODBUS_ERR_INVALID_RESPONSE);

    // Write 系の応答: ファンクションコードとアドレスの一致
    const uint8_t w6[] = {0x06, 0x00, 0x01, 0x00, 0x03};
    CHECK_EQ(modbus_master_parse_write_resp(w6, sizeof w6, 0x06, 1), MODBUS_OK);
    CHECK_EQ(modbus_master_parse_write_resp(w6, sizeof w6, 0x05, 1), MODBUS_ERR_FUNCTION_MISMATCH);
    CHECK_EQ(modbus_master_parse_write_resp(w6, sizeof w6, 0x06, 2), MODBUS_ERR_ADDRESS_MISMATCH);
    CHECK_EQ(modbus_master_parse_write_resp(w6, 4, 0x06, 1), MODBUS_ERR_INVALID_PARAM);
}

TEST_CASE(spec_master_request_quantity_limits) {
    uint8_t pdu[MODBUS_MAX_PDU_LENGTH];
    size_t n = 0;
    // 読み出し: コイル 1..2000、レジスタ 1..125
    CHECK(!modbus_master_req_read_coils(0, 0, pdu, &n));
    CHECK(modbus_master_req_read_coils(0, 2000, pdu, &n));
    CHECK_BYTES(pdu, n, 0x01, 0x00, 0x00, 0x07, 0xD0);
    CHECK(!modbus_master_req_read_coils(0, 2001, pdu, &n));
    CHECK(!modbus_master_req_read_discrete_inputs(0, 0, pdu, &n));
    CHECK(modbus_master_req_read_discrete_inputs(0, 2000, pdu, &n));
    CHECK(!modbus_master_req_read_discrete_inputs(0, 2001, pdu, &n));
    CHECK(!modbus_master_req_read_holding_registers(0, 0, pdu, &n));
    CHECK(modbus_master_req_read_holding_registers(0xFFFF, 125, pdu, &n));
    CHECK_BYTES(pdu, n, 0x03, 0xFF, 0xFF, 0x00, 0x7D);
    CHECK(!modbus_master_req_read_holding_registers(0, 126, pdu, &n));
    CHECK(!modbus_master_req_read_input_registers(0, 0, pdu, &n));
    CHECK(modbus_master_req_read_input_registers(0, 125, pdu, &n));
    CHECK(!modbus_master_req_read_input_registers(0, 126, pdu, &n));

    // 書き込み: コイル 1..1968、レジスタ 1..123
    Bytes coil_data(246 + 1, 0xFF);
    std::vector<uint16_t> reg_data(124, 0xFFFF);
    CHECK(!modbus_master_req_write_multiple_coils(0, 0, coil_data.data(), pdu, &n));
    CHECK(modbus_master_req_write_multiple_coils(0, 1968, coil_data.data(), pdu, &n));
    CHECK_EQ(n, 6u + 246u);
    CHECK_BYTES(pdu, 6, 0x0F, 0x00, 0x00, 0x07, 0xB0, 246);
    CHECK(!modbus_master_req_write_multiple_coils(0, 1969, coil_data.data(), pdu, &n));
    CHECK(!modbus_master_req_write_multiple_registers(0, 0, reg_data.data(), pdu, &n));
    CHECK(modbus_master_req_write_multiple_registers(0, 123, reg_data.data(), pdu, &n));
    CHECK_EQ(n, 6u + 246u);
    CHECK_BYTES(pdu, 6, 0x10, 0x00, 0x00, 0x00, 0x7B, 246);
    CHECK(!modbus_master_req_write_multiple_registers(0, 124, reg_data.data(), pdu, &n));

    // 個数がバイト境界でない場合のバイト数: 1個 -> 1, 8個 -> 1, 9個 -> 2
    const uint8_t two[] = {0xFF, 0x01};
    CHECK(modbus_master_req_write_multiple_coils(0, 1, two, pdu, &n));
    CHECK_BYTES(pdu, 7, 0x0F, 0, 0, 0, 1, 1, 0xFF);
    CHECK(modbus_master_req_write_multiple_coils(0, 8, two, pdu, &n));
    CHECK_EQ(pdu[5], 1u);
    CHECK(modbus_master_req_write_multiple_coils(0, 9, two, pdu, &n));
    CHECK_EQ(pdu[5], 2u);
    CHECK_EQ(n, 8u);
}

TEST_CASE(spec_16bit_fields_are_big_endian_and_invalid_arguments_are_rejected) {
    uint8_t pdu[MODBUS_MAX_PDU_LENGTH];
    size_t n = 0;
    // アドレス・個数・値は上位バイトが先 (ビッグエンディアン)。上位バイトも使う値で確認する
    REQUIRE(modbus_master_req_write_single_coil(0x1234, true, pdu, &n));
    CHECK_BYTES(pdu, n, 0x05, 0x12, 0x34, 0xFF, 0x00);
    REQUIRE(modbus_master_req_write_single_register(0x1234, 0xABCD, pdu, &n));
    CHECK_BYTES(pdu, n, 0x06, 0x12, 0x34, 0xAB, 0xCD);
    const uint16_t regs[] = {0x1234, 0xABCD};
    REQUIRE(modbus_master_req_write_multiple_registers(0x8001, 2, regs, pdu, &n));
    CHECK_BYTES(pdu, n, 0x10, 0x80, 0x01, 0x00, 0x02, 0x04, 0x12, 0x34, 0xAB, 0xCD);
    const uint8_t coils[] = {0x01};
    REQUIRE(modbus_master_req_write_multiple_coils(0x8001, 1, coils, pdu, &n));
    CHECK_BYTES(pdu, n, 0x0F, 0x80, 0x01, 0x00, 0x01, 0x01, 0x01);
    // 書き込み応答のアドレスは上位バイトも比較する
    const uint8_t w[] = {0x06, 0x12, 0x34, 0x00, 0x01};
    CHECK_EQ(modbus_master_parse_write_resp(w, sizeof w, 0x06, 0x1234), MODBUS_OK);
    CHECK_EQ(modbus_master_parse_write_resp(w, sizeof w, 0x06, 0x0034), MODBUS_ERR_ADDRESS_MISMATCH);

    // 個数の上位バイトも解釈される (257 個は 1 個に切り詰められない)
    Device d;
    modbus_slave_t slave;
    init_slave(slave, d);
    CHECK_PDU(transact(slave, {0x03, 0x00, 0x00, 0x01, 0x01}), 0x83, 0x03);
    CHECK_PDU(transact(slave, {0x01, 0x10, 0x00, 0x00, 0x00}), 0x81, 0x03);
    // アドレスの上位バイトも解釈される。0x0F00 は範囲内、0x1000 以上は範囲外 (デバイスは 0..4095)
    d.holding[0x0F00] = 0x4321;
    CHECK_PDU(transact(slave, {0x03, 0x0F, 0x00, 0x00, 0x01}), 0x03, 0x02, 0x43, 0x21);
    CHECK_PDU(transact(slave, {0x03, 0x10, 0x00, 0x00, 0x01}), 0x83, 0x02);

    // NULL・長さ0などの不正な引数は拒否される
    size_t rl = 0;
    uint8_t resp[16];
    const uint8_t req[] = {0x03, 0x00, 0x00, 0x00, 0x01};
    CHECK(!modbus_slave_process_pdu(nullptr, req, 5, resp, &rl));
    CHECK(!modbus_slave_process_pdu(&slave, nullptr, 5, resp, &rl));
    CHECK(!modbus_slave_process_pdu(&slave, req, 0, resp, &rl));
    CHECK(!modbus_slave_process_pdu(&slave, req, 5, nullptr, &rl));
    CHECK(!modbus_slave_process_pdu(&slave, req, 5, resp, nullptr));
    CHECK(!modbus_master_req_write_single_coil(0, true, nullptr, &n));
    CHECK(!modbus_master_req_write_single_coil(0, true, pdu, nullptr));
    CHECK(!modbus_master_req_write_single_register(0, 0, nullptr, &n));
    CHECK(!modbus_master_req_write_multiple_registers(0, 1, nullptr, pdu, &n));
    CHECK(!modbus_master_req_write_multiple_coils(0, 1, nullptr, pdu, &n));
    CHECK_EQ(modbus_master_parse_read_registers_resp(nullptr, 8, nullptr, 3), MODBUS_ERR_INVALID_PARAM);
    const uint8_t one[] = {0x03};
    uint16_t out[4];
    CHECK_EQ(modbus_master_parse_read_registers_resp(one, 1, out, 1), MODBUS_ERR_INVALID_PARAM);
    CHECK_EQ(modbus_master_parse_read_coils_resp(one, 1, resp, 1), MODBUS_ERR_INVALID_PARAM);
    CHECK_EQ(modbus_master_parse_write_resp(one, 1, 0x06, 0), MODBUS_ERR_INVALID_PARAM);
    CHECK_EQ(modbus_crc16(nullptr, 4), 0u);
    CHECK_EQ(modbus_lrc(nullptr, 4), 0u);
    uint8_t adu[8] = {};
    CHECK(!modbus_rtu_slave_handle(&slave, adu, 3, resp, &rl)); // 最小フレーム未満
    CHECK(!modbus_rtu_slave_handle(nullptr, adu, 8, resp, &rl));
}

EMBED_TEST_MAIN()
