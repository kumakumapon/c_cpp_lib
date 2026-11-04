// Modbus TCP (MBAP) のバイト列を、Modbus Application Protocol V1.1b3 の例
// (FC03: 0x006B から 3 レジスタ) と Messaging on TCP/IP Implementation Guide V1.0b で固定する。
#include "embed_modbus/modbus_master.h"
#include "embed_modbus_tcp/embed_modbus_tcp.h"
#include "embed_test/embed_test.hpp"
#include <cstring>
#include <vector>

namespace {
uint16_t regs[0x70];
modbus_exception_t read_regs(uint16_t addr, uint16_t count, uint16_t *dst, void *) {
    if (addr + count > 0x70)
        return MODBUS_EX_ILLEGAL_DATA_ADDRESS;
    std::memcpy(dst, regs + addr, count * sizeof(uint16_t));
    return MODBUS_EX_NONE;
}
modbus_exception_t write_regs(uint16_t addr, uint16_t count, const uint16_t *src, void *) {
    if (addr + count > 0x70)
        return MODBUS_EX_ILLEGAL_DATA_ADDRESS;
    std::memcpy(regs + addr, src, count * sizeof(uint16_t));
    return MODBUS_EX_NONE;
}
modbus_slave_t make_slave(uint8_t unit) {
    regs[0x6B] = 0x022B;
    regs[0x6C] = 0x0000;
    regs[0x6D] = 0x0064;
    modbus_slave_callbacks_t cb{};
    cb.read_holding_registers = read_regs;
    cb.write_holding_registers = write_regs;
    modbus_slave_t s{};
    modbus_slave_init(&s, unit, &cb, nullptr);
    return s;
}
// 要求: TID=0001 PID=0000 LEN=0006 UID=11 | 03 006B 0003
const uint8_t kReq[] = {0x00, 0x01, 0x00, 0x00, 0x00, 0x06, 0x11, 0x03, 0x00, 0x6B, 0x00, 0x03};
// 応答: TID=0001 PID=0000 LEN=0009 UID=11 | 03 06 022B 0000 0064
const uint8_t kResp[] = {0x00, 0x01, 0x00, 0x00, 0x00, 0x09, 0x11, 0x03,
                         0x06, 0x02, 0x2B, 0x00, 0x00, 0x00, 0x64};

struct Frames {
    std::vector<std::vector<uint8_t>> adus;
};
void collect(const uint8_t *d, size_t n, void *u) {
    static_cast<Frames *>(u)->adus.emplace_back(d, d + n);
}
} // namespace

TEST_CASE(server_response_matches_spec_example) {
    modbus_slave_t s = make_slave(0x11);
    uint8_t out[EMBED_MODBUS_TCP_MAX_ADU];
    REQUIRE_EQ(embed_modbus_tcp_handle(&s, kReq, sizeof(kReq), out, sizeof(out)), sizeof(kResp));
    CHECK_MEM_EQ(out, kResp, sizeof(kResp));
}

TEST_CASE(unit_identifier_direct_addressing) {
    modbus_slave_t s = make_slave(0x11);
    uint8_t req[sizeof(kReq)], out[EMBED_MODBUS_TCP_MAX_ADU];
    std::memcpy(req, kReq, sizeof(req));
    for (uint8_t unit : {uint8_t(0xFF), uint8_t(0x00)}) {
        req[6] = unit;
        REQUIRE_EQ(embed_modbus_tcp_handle(&s, req, sizeof(req), out, sizeof(out)), sizeof(kResp));
        CHECK_EQ(out[6], unit); // 要求の Unit ID をそのまま返す
    }
    req[6] = 0x12; // 別の装置宛て: 応答しない
    CHECK_EQ(embed_modbus_tcp_handle(&s, req, sizeof(req), out, sizeof(out)), 0u);
}

TEST_CASE(exception_response_bytes) {
    modbus_slave_t s = make_slave(0x11);
    uint8_t req[sizeof(kReq)], out[EMBED_MODBUS_TCP_MAX_ADU];
    std::memcpy(req, kReq, sizeof(req));
    req[9] = 0x6F; // 0x6F から 3 個は範囲外
    const uint8_t expect[] = {0x00, 0x01, 0x00, 0x00, 0x00, 0x03, 0x11, 0x83, 0x02};
    REQUIRE_EQ(embed_modbus_tcp_handle(&s, req, sizeof(req), out, sizeof(out)), sizeof(expect));
    CHECK_MEM_EQ(out, expect, sizeof(expect));
}

TEST_CASE(mbap_validation) {
    modbus_slave_t s = make_slave(0x11);
    uint8_t req[sizeof(kReq) + 1], out[EMBED_MODBUS_TCP_MAX_ADU];
    std::memcpy(req, kReq, sizeof(kReq));
    req[3] = 0x01; // Protocol ID != 0
    CHECK_EQ(embed_modbus_tcp_handle(&s, req, sizeof(kReq), out, sizeof(out)), 0u);
    std::memcpy(req, kReq, sizeof(kReq));
    CHECK_EQ(embed_modbus_tcp_handle(&s, req, sizeof(kReq) - 1, out, sizeof(out)), 0u); // 短い
    CHECK_EQ(embed_modbus_tcp_handle(&s, req, sizeof(kReq) + 1, out, sizeof(out)), 0u); // 長い
    CHECK_EQ(embed_modbus_tcp_handle(&s, req, sizeof(kReq), out, 259), 0u); // 出力容量不足
    CHECK_EQ(embed_modbus_tcp_handle(nullptr, req, sizeof(kReq), out, sizeof(out)), 0u);
    embed_mbap h{};
    const uint8_t len1[] = {0, 0, 0, 0, 0, 1, 0x11};
    const uint8_t len255[] = {0, 0, 0, 0, 0, 255, 0x11};
    const uint8_t len2[] = {0, 7, 0, 0, 0, 2, 0x11};
    CHECK_EQ(embed_mbap_decode(len1, 7, &h), 0u);
    CHECK_EQ(embed_mbap_decode(len255, 7, &h), 0u);
    REQUIRE_EQ(embed_mbap_decode(len2, 7, &h), 7u); // 最小 (Unit ID + FC)
    CHECK_EQ(h.transaction, 7u);
    CHECK_EQ(h.length, 2u);
    CHECK_EQ(embed_mbap_decode(len2, 6, &h), 0u);
    embed_mbap e{0xBEEF, 254, 0xFF};
    uint8_t hdr[7];
    REQUIRE_EQ(embed_mbap_encode(&e, hdr, 7), 7u);
    const uint8_t hdr_expect[] = {0xBE, 0xEF, 0x00, 0x00, 0x00, 0xFE, 0xFF};
    CHECK_MEM_EQ(hdr, hdr_expect, 7);
    CHECK_EQ(embed_mbap_encode(&e, hdr, 6), 0u);
    e.length = 1;
    CHECK_EQ(embed_mbap_encode(&e, hdr, 7), 0u);
}

TEST_CASE(framer_splits_stream_at_every_boundary) {
    std::vector<uint8_t> stream(kReq, kReq + sizeof(kReq));
    stream.insert(stream.end(), kResp, kResp + sizeof(kResp));
    for (size_t split = 0; split <= stream.size(); ++split) {
        embed_modbus_tcp_connection c{};
        Frames f;
        CHECK(embed_modbus_tcp_frame(&c, stream.data(), split, collect, &f));
        CHECK(embed_modbus_tcp_frame(&c, stream.data() + split, stream.size() - split, collect, &f));
        REQUIRE_EQ(f.adus.size(), 2u);
        CHECK(f.adus[0] == std::vector<uint8_t>(kReq, kReq + sizeof(kReq)));
        CHECK(f.adus[1] == std::vector<uint8_t>(kResp, kResp + sizeof(kResp)));
        CHECK_EQ(c.used, 0u);
    }
    // 不正な MBAP は failed を保持
    embed_modbus_tcp_connection c{};
    Frames f;
    uint8_t bad[sizeof(kReq)];
    std::memcpy(bad, kReq, sizeof(bad));
    bad[2] = 0x12;
    CHECK(!embed_modbus_tcp_frame(&c, bad, sizeof(bad), collect, &f));
    CHECK(c.failed);
    CHECK(!embed_modbus_tcp_frame(&c, kReq, sizeof(kReq), collect, &f));
    CHECK(f.adus.empty());
    embed_modbus_tcp_connection d{};
    CHECK(!embed_modbus_tcp_frame(&d, kReq, sizeof(kReq), nullptr, &f));
    CHECK(!embed_modbus_tcp_frame(&d, nullptr, 3, collect, &f));
    CHECK(embed_modbus_tcp_frame(&d, nullptr, 0, collect, &f));
}

TEST_CASE(feed_answers_fragmented_requests) {
    modbus_slave_t s = make_slave(0x11);
    embed_modbus_tcp_connection c{};
    Frames f;
    for (uint8_t b : kReq)
        REQUIRE(embed_modbus_tcp_feed(&c, &s, &b, 1, collect, &f));
    REQUIRE_EQ(f.adus.size(), 1u);
    CHECK(f.adus[0] == std::vector<uint8_t>(kResp, kResp + sizeof(kResp)));
    CHECK(!embed_modbus_tcp_feed(&c, nullptr, kReq, 1, collect, &f));
    CHECK(!embed_modbus_tcp_feed(&c, &s, kReq, 1, nullptr, &f));
}

TEST_CASE(client_request_and_response_round_trip) {
    uint8_t pdu[5];
    size_t pdu_len = 0;
    REQUIRE(modbus_master_req_read_holding_registers(0x006B, 3, pdu, &pdu_len));
    uint8_t adu[EMBED_MODBUS_TCP_MAX_ADU];
    REQUIRE_EQ(embed_modbus_tcp_build_request(0x0001, 0x11, pdu, pdu_len, adu, sizeof(adu)),
               sizeof(kReq));
    CHECK_MEM_EQ(adu, kReq, sizeof(kReq));
    const uint8_t *rpdu = nullptr;
    size_t rlen = 0;
    REQUIRE_EQ(embed_modbus_tcp_parse_response(kResp, sizeof(kResp), 0x0001, 0x11, &rpdu, &rlen),
               MODBUS_OK);
    CHECK(rpdu == kResp + 7);
    CHECK_EQ(rlen, 8u);
    uint16_t values[3] = {};
    CHECK_EQ(modbus_master_parse_read_registers_resp(rpdu, rlen, values, 3), MODBUS_OK);
    CHECK_EQ(values[0], 0x022Bu);
    CHECK_EQ(values[2], 0x0064u);
    CHECK_EQ(embed_modbus_tcp_parse_response(kResp, sizeof(kResp), 0x0002, 0x11, &rpdu, &rlen),
             MODBUS_ERR_TRANSACTION_MISMATCH);
    CHECK_EQ(embed_modbus_tcp_parse_response(kResp, sizeof(kResp), 0x0001, 0x12, &rpdu, &rlen),
             MODBUS_ERR_ADDRESS_MISMATCH);
    CHECK_EQ(embed_modbus_tcp_parse_response(kResp, sizeof(kResp) - 1, 0x0001, 0x11, &rpdu, &rlen),
             MODBUS_ERR_INVALID_RESPONSE);
    CHECK_EQ(embed_modbus_tcp_parse_response(kResp, sizeof(kResp), 0x0001, 0x11, nullptr, &rlen),
             MODBUS_ERR_INVALID_PARAM);
    // build_request の境界
    static uint8_t big[254];
    big[0] = 0x10;
    CHECK_EQ(embed_modbus_tcp_build_request(1, 1, big, 253, adu, sizeof(adu)), 260u);
    CHECK_EQ(embed_modbus_tcp_build_request(1, 1, big, 254, adu, sizeof(adu)), 0u);
    CHECK_EQ(embed_modbus_tcp_build_request(1, 1, big, 0, adu, sizeof(adu)), 0u);
    CHECK_EQ(embed_modbus_tcp_build_request(1, 1, big, 5, adu, 11), 0u);
    CHECK_EQ(embed_modbus_tcp_build_request(1, 1, big, 5, adu, 12), 12u);
    CHECK_EQ(embed_modbus_tcp_build_request(1, 1, nullptr, 5, adu, 12), 0u);
}

TEST_CASE(client_server_write_then_read) {
    modbus_slave_t s = make_slave(0x01);
    uint8_t pdu[MODBUS_MAX_PDU_LENGTH], req[EMBED_MODBUS_TCP_MAX_ADU], resp[EMBED_MODBUS_TCP_MAX_ADU];
    size_t pdu_len = 0;
    const uint16_t values[2] = {1234, 5678};
    REQUIRE(modbus_master_req_write_multiple_registers(0x10, 2, values, pdu, &pdu_len));
    size_t n = embed_modbus_tcp_build_request(7, EMBED_MODBUS_TCP_UNIT_DIRECT, pdu, pdu_len, req, sizeof(req));
    size_t m = embed_modbus_tcp_handle(&s, req, n, resp, sizeof(resp));
    const uint8_t *rpdu = nullptr;
    size_t rlen = 0;
    REQUIRE_EQ(embed_modbus_tcp_parse_response(resp, m, 7, 0xFF, &rpdu, &rlen), MODBUS_OK);
    CHECK_EQ(modbus_master_parse_write_resp(rpdu, rlen, MODBUS_FC_WRITE_MULTIPLE_REGISTERS, 0x10),
             MODBUS_OK);
    CHECK_EQ(regs[0x10], 1234u);
    CHECK_EQ(regs[0x11], 5678u);
}

TEST_CASE(transaction_timeouts) {
    embed_modbus_transactions t{};
    uint16_t a = 0, b = 0, c = 0, untimed = 0;
    REQUIRE(embed_modbus_transaction_begin_at(&t, 1000, &a));
    REQUIRE(embed_modbus_transaction_begin_at(&t, 1005, &b));
    REQUIRE(embed_modbus_transaction_begin_at(&t, 1010, &c));
    REQUIRE(embed_modbus_transaction_begin(&t, &untimed));
    CHECK_EQ(embed_modbus_transaction_pending(&t), 4u);
    uint16_t out[4] = {};
    // 時刻は単調増加で渡すこと (最後の begin_at より前の時刻は周回とみなされる)
    CHECK_EQ(embed_modbus_transaction_expire(&t, 1010, 11, out, 4), 0u); // a は 10 ms: まだ
    REQUIRE_EQ(embed_modbus_transaction_expire(&t, 1011, 11, out, 4), 1u); // a は 11 ms ちょうど
    CHECK_EQ(out[0], a);
    CHECK(!embed_modbus_transaction_end(&t, a)); // 期限切れで解放済み
    REQUIRE_EQ(embed_modbus_transaction_expire(&t, 5000, 10, out, 1), 1u); // cap で打ち切り
    CHECK_EQ(out[0], b);
    REQUIRE_EQ(embed_modbus_transaction_expire(&t, 5000, 10, nullptr, 0), 1u); // 残りを解放だけ
    CHECK_EQ(embed_modbus_transaction_pending(&t), 1u); // 時刻なしの ID は残る
    CHECK(embed_modbus_transaction_end(&t, untimed));
    // 32bit の周回
    uint16_t w = 0;
    REQUIRE(embed_modbus_transaction_begin_at(&t, 0xFFFFFFF0u, &w));
    CHECK_EQ(embed_modbus_transaction_expire(&t, 0x00000004u, 21, out, 4), 0u); // 経過 20
    CHECK_EQ(embed_modbus_transaction_expire(&t, 0x00000005u, 21, out, 4), 1u); // 経過 21
    CHECK_EQ(embed_modbus_transaction_expire(nullptr, 0, 0, out, 4), 0u);
    CHECK_EQ(embed_modbus_transaction_pending(nullptr), 0u);
    CHECK(!embed_modbus_transaction_begin_at(nullptr, 0, &w));
}

EMBED_TEST_MAIN()
