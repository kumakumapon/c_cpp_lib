// EtherNet/IP カプセル化ヘッダ・CPF・EPATH・CIP 要求/応答の固定バイト列 (リトルエンディアン)、
// セッション管理、Identity / Assembly オブジェクトの一般ステータスを固定するテスト。
#include "embed_test/embed_test.hpp"
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

#include "embed_eip/eip_adapter.h"
#include "embed_eip/eip_cip.h"
#include "embed_eip/eip_encap.h"
#include "embed_eip/eip_types.h"

namespace {
using Bytes = std::vector<uint8_t>;

bool same(const Bytes &a, const Bytes &e, const char *file, int line) {
    const bool ok = embed_test::check(a == e, "bytes equality", file, line);
    if (!ok) {
        std::printf("# expected:");
        for (uint8_t b : e) std::printf(" %02X", b);
        std::printf("\n# actual:  ");
        for (uint8_t b : a) std::printf(" %02X", b);
        std::printf("\n");
    }
    return ok;
}
#define CHECK_BYTES(actual, ...) same((actual), Bytes{__VA_ARGS__}, __FILE__, __LINE__)

Bytes cat(std::initializer_list<Bytes> parts) {
    Bytes out;
    for (const Bytes &p : parts) out.insert(out.end(), p.begin(), p.end());
    return out;
}

const uint8_t kCtx[8] = {1, 2, 3, 4, 5, 6, 7, 8};
const Bytes kCtxBytes = {1, 2, 3, 4, 5, 6, 7, 8};

Bytes u16(uint16_t v) { return {uint8_t(v & 0xFF), uint8_t(v >> 8)}; }
Bytes u32(uint32_t v) { return {uint8_t(v), uint8_t(v >> 8), uint8_t(v >> 16), uint8_t(v >> 24)}; }

// カプセル化パケット: ヘッダ 24 バイト + ペイロード (length はペイロード長)
Bytes encap(uint16_t cmd, uint32_t session, const Bytes &payload, uint32_t status = 0) {
    return cat({u16(cmd), u16((uint16_t)payload.size()), u32(session), u32(status), kCtxBytes, u32(0), payload});
}

// SendRRData の CPF ペイロード (Null Address + Unconnected Data)
Bytes cpf(const Bytes &cip) {
    return cat({u32(0), u16(0), u16(2), u16(0x0000), u16(0), u16(0x00B2), u16((uint16_t)cip.size()), cip});
}

Bytes rr(uint32_t session, const Bytes &cip) { return encap(EIP_CMD_SEND_RR_DATA, session, cpf(cip)); }

const Bytes kRegisterPayload = {0x01, 0x00, 0x00, 0x00}; // プロトコルバージョン 1, オプション 0

Bytes process(eip_adapter_t &a, const Bytes &req, size_t max = 1024) {
    Bytes out(max);
    const size_t n = eip_adapter_process_packet(&a, req.data(), req.size(), out.data(), out.size());
    out.resize(n);
    return out;
}

struct Reply {
    eip_encap_header_t h{};
    Bytes payload;
    bool valid = false;
};
Reply parse(const Bytes &b) {
    Reply r;
    if (b.size() < EIP_ENCAP_HEADER_SIZE) return r;
    eip_decode_encap_header(b.data(), b.size(), &r.h);
    r.payload.assign(b.begin() + EIP_ENCAP_HEADER_SIZE, b.end());
    r.valid = true;
    return r;
}

// SendRRData の応答から CIP 応答を取り出す: {service, reserved, status, addl_size, data...}
Bytes cip_of(const Bytes &resp) {
    const Reply r = parse(resp);
    const uint8_t *d = nullptr;
    size_t n = 0;
    if (!r.valid || !eip_parse_send_rr_data(r.payload.data(), r.payload.size(), &d, &n)) return {};
    return Bytes(d, d + n);
}

struct Dev {
    eip_adapter_t a;
    uint32_t session = 0;
    explicit Dev(const cip_identity_attr_t *id = nullptr) {
        eip_adapter_init(&a, id);
        const Reply r = parse(process(a, encap(EIP_CMD_REGISTER_SESSION, 0, kRegisterPayload)));
        session = r.h.session_handle;
    }
    Bytes cip(const Bytes &req) { return cip_of(process(a, rr(session, req))); }
};

cip_identity_attr_t make_identity() {
    cip_identity_attr_t id{};
    id.vendor_id = 0x1234;
    id.device_type = 0x000C;
    id.product_code = 0xBEEF;
    id.major_revision = 3;
    id.minor_revision = 14;
    id.status = 0x0060;
    id.serial_number = 0xA1B2C3D4;
    std::snprintf(id.product_name, sizeof(id.product_name), "%s", "Test");
    return id;
}

Bytes get_attr(Dev &d, uint16_t cls, uint16_t inst, uint16_t attr) {
    uint8_t buf[64];
    const size_t n = cip_build_get_attribute_single(cls, inst, attr, buf, sizeof(buf));
    return d.cip(Bytes(buf, buf + n));
}
} // namespace

// ---- カプセル化ヘッダ ----
TEST_CASE(encapsulation_constants) {
    CHECK_EQ(EIP_ENCAP_HEADER_SIZE, 24);
    CHECK_EQ(EIP_DEFAULT_TCP_PORT, 44818);
    CHECK_EQ(EIP_DEFAULT_UDP_PORT, 2222);
    CHECK_EQ(EIP_PROTOCOL_VERSION, 1);
    CHECK_EQ((int)EIP_CMD_NOP, 0x0000);
    CHECK_EQ((int)EIP_CMD_LIST_SERVICES, 0x0004);
    CHECK_EQ((int)EIP_CMD_LIST_IDENTITY, 0x0063);
    CHECK_EQ((int)EIP_CMD_REGISTER_SESSION, 0x0065);
    CHECK_EQ((int)EIP_CMD_UNREGISTER_SESSION, 0x0066);
    CHECK_EQ((int)EIP_CMD_SEND_RR_DATA, 0x006F);
    CHECK_EQ((int)EIP_CMD_SEND_UNIT_DATA, 0x0070);
    CHECK_EQ((int)EIP_STATUS_INVALID_CMD, 0x01);
    CHECK_EQ((int)EIP_STATUS_NO_MEMORY, 0x02);
    CHECK_EQ((int)EIP_STATUS_INCORRECT_DATA, 0x03);
    CHECK_EQ((int)EIP_STATUS_INVALID_SESSION, 0x64);
    CHECK_EQ((int)EIP_STATUS_INVALID_LENGTH, 0x65);
    CHECK_EQ((int)EIP_STATUS_UNSUPPORTED_VER, 0x69);
    CHECK_EQ((int)CIP_SERVICE_GET_ATTRIBUTE_SINGLE, 0x0E);
    CHECK_EQ((int)CIP_SERVICE_SET_ATTRIBUTE_SINGLE, 0x10);
    CHECK_EQ((int)CIP_CLASS_IDENTITY, 0x01);
    CHECK_EQ((int)CIP_CLASS_ASSEMBLY, 0x04);
    CHECK_EQ((int)EIP_ITEM_NULL_ADDRESS, 0x0000);
    CHECK_EQ((int)EIP_ITEM_CONNECTED_DATA, 0x00B1);
    CHECK_EQ((int)EIP_ITEM_UNCONNECTED_DATA, 0x00B2);
}

TEST_CASE(encapsulation_header_bytes_are_little_endian) {
    eip_encap_header_t h{};
    h.command = EIP_CMD_REGISTER_SESSION;
    h.length = 4;
    h.session_handle = 0x11223344;
    h.status = 0x55667788;
    std::memcpy(h.sender_context, kCtx, 8);
    h.options = 0x99AABBCC;
    uint8_t buf[32];
    std::memset(buf, 0xEE, sizeof(buf));
    CHECK_EQ(eip_encode_encap_header(&h, buf), 24u);
    CHECK_BYTES(Bytes(buf, buf + 24), 0x65, 0x00, 0x04, 0x00, 0x44, 0x33, 0x22, 0x11, 0x88, 0x77, 0x66, 0x55,
                0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0xCC, 0xBB, 0xAA, 0x99);
    CHECK_EQ(buf[24], 0xEE); // 24 バイトより先は書かない

    eip_encap_header_t back{};
    CHECK_EQ(eip_decode_encap_header(buf, 24, &back), 24u);
    CHECK_EQ(back.command, 0x0065);
    CHECK_EQ(back.length, 4u);
    CHECK_EQ(back.session_handle, 0x11223344u);
    CHECK_EQ(back.status, 0x55667788u);
    CHECK(std::memcmp(back.sender_context, kCtx, 8) == 0);
    CHECK_EQ(back.options, 0x99AABBCCu);

    // 上位ビットの立ったバイトも符号拡張されない
    const uint8_t hi[24] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x80, 0x00, 0x00, 0x80};
    CHECK_EQ(eip_decode_encap_header(hi, 24, &back), 24u);
    CHECK_EQ(back.command, 0xFFFF);
    CHECK_EQ(back.session_handle, 0xFFFFFFFFu);
    CHECK_EQ(back.status, 0x80000080u);

    CHECK_EQ(eip_decode_encap_header(buf, 23, &back), 0u);
    CHECK_EQ(eip_decode_encap_header(nullptr, 24, &back), 0u);
    CHECK_EQ(eip_decode_encap_header(buf, 24, nullptr), 0u);
    CHECK_EQ(eip_encode_encap_header(nullptr, buf), 0u);
    CHECK_EQ(eip_encode_encap_header(&h, nullptr), 0u);
}

TEST_CASE(register_session_response_bytes) {
    uint8_t buf[64];
    std::memset(buf, 0xEE, sizeof(buf)); // 書き残しがあれば 0xEE が残る
    CHECK_EQ(eip_build_register_session_resp(0x00001234, kCtx, buf, sizeof(buf)), 28u);
    CHECK_BYTES(Bytes(buf, buf + 28), 0x65, 0x00, 0x04, 0x00, 0x34, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00);
    CHECK_EQ(eip_build_register_session_resp(1, kCtx, buf, 28), 28u);
    CHECK_EQ(eip_build_register_session_resp(1, kCtx, buf, 27), 0u);
    CHECK_EQ(eip_build_register_session_resp(1, kCtx, nullptr, 64), 0u);
    // コンテキストなしは 0 埋め
    CHECK_EQ(eip_build_register_session_resp(1, nullptr, buf, sizeof(buf)), 28u);
    for (int i = 12; i < 20; ++i) CHECK_EQ(buf[i], 0);
}

TEST_CASE(send_rr_data_bytes_and_limits) {
    const Bytes cip = {0x8E, 0x00, 0x00, 0x00, 0xAA, 0xBB};
    uint8_t buf[128];
    const size_t n = eip_build_send_rr_data(0xAABBCCDD, kCtx, cip.data(), cip.size(), buf, sizeof(buf));
    CHECK_EQ(n, 24u + 16 + cip.size());
    CHECK_BYTES(Bytes(buf, buf + n), 0x6F, 0x00, 0x16, 0x00,       // SendRRData, 長さ 16 + 6
                0xDD, 0xCC, 0xBB, 0xAA, 0x00, 0x00, 0x00, 0x00,    // セッション, ステータス
                0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,    // コンテキスト
                0x00, 0x00, 0x00, 0x00,                            // オプション
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00,                // インターフェースハンドル, タイムアウト
                0x02, 0x00,                                        // アイテム数
                0x00, 0x00, 0x00, 0x00,                            // Null Address
                0xB2, 0x00, 0x06, 0x00,                            // Unconnected Data, 長さ 6
                0x8E, 0x00, 0x00, 0x00, 0xAA, 0xBB);
    CHECK_EQ(eip_build_send_rr_data(1, kCtx, cip.data(), cip.size(), buf, n), n);
    CHECK_EQ(eip_build_send_rr_data(1, kCtx, cip.data(), cip.size(), buf, n - 1), 0u);
    CHECK_EQ(eip_build_send_rr_data(1, kCtx, cip.data(), 0, buf, sizeof(buf)), 0u);
    CHECK_EQ(eip_build_send_rr_data(1, kCtx, nullptr, 6, buf, sizeof(buf)), 0u);
    CHECK_EQ(eip_build_send_rr_data(1, kCtx, cip.data(), 6, nullptr, sizeof(buf)), 0u);
    // ヘッダの長さは 16 ビット: 収まらない CIP データは作らない
    std::vector<uint8_t> big(70000, 0x11), dst(80000);
    CHECK_EQ(eip_build_send_rr_data(1, kCtx, big.data(), 65519, dst.data(), dst.size()), 24u + 16 + 65519);
    CHECK_EQ(eip_build_send_rr_data(1, kCtx, big.data(), 65520, dst.data(), dst.size()), 0u);
}

TEST_CASE(parse_send_rr_data_items) {
    const uint8_t *d = nullptr;
    size_t n = 0;
    const Bytes ok = cpf({0x0E, 0x03});
    CHECK(eip_parse_send_rr_data(ok.data(), ok.size(), &d, &n));
    CHECK_EQ(n, 2u);
    CHECK_EQ(d[0], 0x0E);
    CHECK_EQ(d - ok.data(), (ptrdiff_t)(ok.size() - 2));

    // Connected Data Item (0xB1) も CIP データとして扱う
    Bytes conn = ok;
    conn[12] = 0xB1; // 2 つ目のアイテム (Unconnected Data) の種別を Connected Data に
    CHECK(eip_parse_send_rr_data(conn.data(), conn.size(), &d, &n));
    // 未知のアイテムは読み飛ばす
    const Bytes skip = cat({u32(0), u16(0), u16(3), u16(0), u16(0), u16(0x8000), u16(2), Bytes{9, 9}, u16(0x00B2),
                            u16(1), Bytes{0x77}});
    CHECK(eip_parse_send_rr_data(skip.data(), skip.size(), &d, &n));
    CHECK_EQ(n, 1u);
    CHECK_EQ(d[0], 0x77);

    // データアイテムがない / 件数 0 / 切り詰め
    const Bytes none = cat({u32(0), u16(0), u16(1), u16(0), u16(0)});
    CHECK(!eip_parse_send_rr_data(none.data(), none.size(), &d, &n));
    const Bytes zero = cat({u32(0), u16(0), u16(0), Bytes(8, 0)});
    CHECK(!eip_parse_send_rr_data(zero.data(), zero.size(), &d, &n));
    CHECK(!eip_parse_send_rr_data(ok.data(), ok.size() - 1, &d, &n));   // アイテム長がペイロードを超える
    CHECK(!eip_parse_send_rr_data(ok.data(), 15, &d, &n));
    CHECK(eip_parse_send_rr_data(ok.data(), 16 + 2, &d, &n));
    CHECK(!eip_parse_send_rr_data(nullptr, 20, &d, &n));
    CHECK(!eip_parse_send_rr_data(ok.data(), ok.size(), nullptr, &n));
    CHECK(!eip_parse_send_rr_data(ok.data(), ok.size(), &d, nullptr));
}

// ---- EPATH / CIP ----
TEST_CASE(epath_segment_widths_switch_at_256) {
    auto enc = [](uint16_t c, uint16_t i, uint16_t a, bool has) {
        cip_epath_t p{};
        p.class_id = c;
        p.instance_id = i;
        p.attribute_id = a;
        p.has_attribute = has;
        uint8_t buf[32];
        const size_t n = cip_encode_epath(&p, buf);
        return Bytes(buf, buf + n);
    };
    CHECK_BYTES(enc(0x01, 0x01, 0x07, true), 0x20, 0x01, 0x24, 0x01, 0x30, 0x07);
    CHECK_BYTES(enc(0xFF, 0xFF, 0xFF, true), 0x20, 0xFF, 0x24, 0xFF, 0x30, 0xFF);
    CHECK_BYTES(enc(0x100, 0x100, 0x100, true), 0x21, 0x00, 0x00, 0x01, 0x25, 0x00, 0x00, 0x01, 0x31, 0x00, 0x00,
                0x01);
    CHECK_BYTES(enc(0xABCD, 0x1234, 0x5678, true), 0x21, 0x00, 0xCD, 0xAB, 0x25, 0x00, 0x34, 0x12, 0x31, 0x00,
                0x78, 0x56);
    CHECK_BYTES(enc(0x04, 0x64, 0, false), 0x20, 0x04, 0x24, 0x64); // 属性なし
    CHECK_BYTES(enc(0x100, 0x01, 0, false), 0x21, 0x00, 0x00, 0x01, 0x24, 0x01);
    CHECK_BYTES(enc(0x01, 0x100, 0, false), 0x20, 0x01, 0x25, 0x00, 0x00, 0x01);
    CHECK_BYTES(enc(0x01, 0x01, 0x100, true), 0x20, 0x01, 0x24, 0x01, 0x31, 0x00, 0x00, 0x01);
    cip_epath_t p{};
    uint8_t buf[8];
    CHECK_EQ(cip_encode_epath(nullptr, buf), 0u);
    CHECK_EQ(cip_encode_epath(&p, nullptr), 0u);
}

TEST_CASE(epath_decoding) {
    cip_epath_t p;
    auto dec = [&](Bytes b) { return cip_decode_epath(b.data(), b.size(), &p); };
    CHECK_EQ(dec({0x20, 0x01, 0x24, 0x02, 0x30, 0x03}), 6u);
    CHECK_EQ(p.class_id, 1u);
    CHECK_EQ(p.instance_id, 2u);
    CHECK_EQ(p.attribute_id, 3u);
    CHECK(p.has_attribute);
    CHECK_EQ(dec({0x21, 0x00, 0x02, 0x01, 0x25, 0x00, 0x04, 0x03, 0x31, 0x00, 0x06, 0x05}), 12u);
    CHECK_EQ(p.class_id, 0x0102u);
    CHECK_EQ(p.instance_id, 0x0304u);
    CHECK_EQ(p.attribute_id, 0x0506u);
    CHECK(p.has_attribute);
    CHECK_EQ(dec({0x20, 0x04, 0x24, 0x64}), 4u);
    CHECK(!p.has_attribute);
    CHECK_EQ(p.attribute_id, 0u);
    // 属性が先でも順序に依らず読む
    CHECK_EQ(dec({0x30, 0x07, 0x20, 0x01, 0x24, 0x01}), 6u);
    CHECK_EQ(p.class_id, 1u);
    CHECK_EQ(p.attribute_id, 7u);
    // 短すぎる / 切り詰めたセグメント
    CHECK_EQ(dec({0x20, 0x01, 0x24}), 0u);
    CHECK_EQ(dec({0x20, 0x01, 0x21, 0x00, 0x01}), 0u);
    CHECK_EQ(dec({0x20, 0x01, 0x25, 0x00, 0x01}), 0u);
    CHECK_EQ(dec({0x20, 0x01, 0x31, 0x00, 0x01}), 0u);
    CHECK_EQ(dec({0x20, 0x01, 0x24, 0x01, 0x30}), 0u);
    CHECK_EQ(dec({0x20, 0x01}), 0u); // 4 バイト未満
    // 未知のセグメントに達したら、そこまでを返す
    CHECK_EQ(dec({0x20, 0x01, 0x24, 0x01, 0x91, 0x02}), 5u);
    CHECK_EQ(cip_decode_epath(nullptr, 4, &p), 0u);
    const uint8_t b[4] = {0x20, 1, 0x24, 1};
    CHECK_EQ(cip_decode_epath(b, 4, nullptr), 0u);
}

TEST_CASE(cip_request_bytes) {
    uint8_t buf[64];
    size_t n = cip_build_get_attribute_single(0x01, 0x01, 0x07, buf, sizeof(buf));
    CHECK_BYTES(Bytes(buf, buf + n), 0x0E, 0x03, 0x20, 0x01, 0x24, 0x01, 0x30, 0x07);
    const uint8_t data[] = {0xDE, 0xAD};
    n = cip_build_set_attribute_single(0x04, 0x64, 0x03, data, 2, buf, sizeof(buf));
    CHECK_BYTES(Bytes(buf, buf + n), 0x10, 0x03, 0x20, 0x04, 0x24, 0x64, 0x30, 0x03, 0xDE, 0xAD);
    // 16 ビットのクラス・インスタンス・属性: パスは 12 バイト = 6 ワード
    n = cip_build_get_attribute_single(0x0100, 0x0200, 0x0300, buf, sizeof(buf));
    CHECK_BYTES(Bytes(buf, buf + n), 0x0E, 0x06, 0x21, 0x00, 0x00, 0x01, 0x25, 0x00, 0x00, 0x02, 0x31, 0x00, 0x00,
                0x03);
    // バッファの境界
    CHECK_EQ(cip_build_get_attribute_single(1, 1, 7, buf, 8), 8u);
    CHECK_EQ(cip_build_get_attribute_single(1, 1, 7, buf, 7), 0u);
    CHECK_EQ(cip_build_set_attribute_single(4, 100, 3, data, 2, buf, 10), 10u);
    CHECK_EQ(cip_build_set_attribute_single(4, 100, 3, data, 2, buf, 9), 0u);
    CHECK_EQ(cip_build_get_attribute_single(1, 1, 7, nullptr, 64), 0u);
    // データの長さがあるのにデータが NULL の要求は作らない
    CHECK_EQ(cip_build_set_attribute_single(4, 100, 3, nullptr, 2, buf, sizeof(buf)), 0u);

    cip_request_t req{};
    CHECK_EQ(cip_encode_request(nullptr, buf, 8), 0u);
    CHECK_EQ(cip_encode_request(&req, nullptr, 8), 0u);
}

TEST_CASE(cip_request_decoding) {
    cip_request_t r;
    Bytes keep; // 復号結果 (r.data) が指すバッファを保持する
    auto dec = [&](Bytes b) {
        keep = std::move(b);
        return cip_decode_request(keep.data(), keep.size(), &r);
    };
    const Bytes pkt = {0x10, 0x03, 0x20, 0x04, 0x24, 0x64, 0x30, 0x03, 0xDE, 0xAD};
    CHECK(dec(pkt));
    CHECK_EQ(r.service, 0x10);
    CHECK_EQ(r.path.class_id, 4u);
    CHECK_EQ(r.path.instance_id, 100u);
    CHECK_EQ(r.path.attribute_id, 3u);
    CHECK_EQ(r.data_len, 2u);
    CHECK_EQ(r.data[0], 0xDE);
    CHECK(dec({0x0E, 0x03, 0x20, 0x01, 0x24, 0x01, 0x30, 0x07}));
    CHECK_EQ(r.data_len, 0u);
    CHECK(!dec({0x0E, 0x03, 0x20, 0x01, 0x24, 0x01, 0x30})); // パスが途中で切れる
    CHECK(!dec({0x0E, 0x04, 0x20, 0x01}));
    CHECK(dec({0x0E, 0x00}));                                  // パスなし
    CHECK(!dec({0x0E}));
    CHECK(!cip_decode_request(nullptr, 4, &r));
    const uint8_t b[2] = {0x0E, 0x00};
    CHECK(!cip_decode_request(b, 2, nullptr));
}

TEST_CASE(cip_response_bytes) {
    uint8_t buf[32];
    const uint8_t data[] = {0x34, 0x12};
    cip_response_t resp{};
    resp.service = CIP_SERVICE_GET_ATTRIBUTE_SINGLE;
    resp.general_status = CIP_STATUS_SUCCESS;
    resp.data = data;
    resp.data_len = 2;
    size_t n = cip_encode_response(&resp, buf, sizeof(buf));
    CHECK_BYTES(Bytes(buf, buf + n), 0x8E, 0x00, 0x00, 0x00, 0x34, 0x12); // 応答サービスコードは 0x80 | サービス
    resp.general_status = CIP_STATUS_UNDEFINED_ATTRIBUTE;
    resp.data = nullptr;
    resp.data_len = 0;
    n = cip_encode_response(&resp, buf, sizeof(buf));
    CHECK_BYTES(Bytes(buf, buf + n), 0x8E, 0x00, 0x14, 0x00);
    resp.service = CIP_SERVICE_SET_ATTRIBUTE_SINGLE;
    resp.general_status = CIP_STATUS_ATTRIBUTE_NOT_SETTABLE;
    n = cip_encode_response(&resp, buf, sizeof(buf));
    CHECK_BYTES(Bytes(buf, buf + n), 0x90, 0x00, 0x0E, 0x00);
    CHECK_EQ(cip_encode_response(&resp, buf, 4), 4u);
    CHECK_EQ(cip_encode_response(&resp, buf, 3), 0u);
    resp.data = data;
    resp.data_len = 2;
    CHECK_EQ(cip_encode_response(&resp, buf, 5), 0u);
    CHECK_EQ(cip_encode_response(&resp, buf, 6), 6u);
    CHECK_EQ(cip_encode_response(nullptr, buf, 6), 0u);
    CHECK_EQ(cip_encode_response(&resp, nullptr, 6), 0u);
    CHECK_EQ((int)CIP_STATUS_SERVICE_NOT_SUPPORTED, 0x08);
    CHECK_EQ((int)CIP_STATUS_PATH_DESTINATION_UNKNOWN, 0x05);
    CHECK_EQ((int)CIP_STATUS_UNDEFINED_ATTRIBUTE, 0x14);
    CHECK_EQ((int)CIP_STATUS_ATTRIBUTE_NOT_SETTABLE, 0x0E);
    CHECK_EQ((int)CIP_STATUS_PATH_SIZE_INVALID, 0x26);
}

// ---- セッション ----
TEST_CASE(register_session_allocates_handles_and_replies) {
    eip_adapter_t a;
    eip_adapter_init(&a, nullptr);
    const Bytes req = encap(EIP_CMD_REGISTER_SESSION, 0, kRegisterPayload);
    CHECK_BYTES(process(a, req), 0x65, 0x00, 0x04, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
                0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00);
    const Reply second = parse(process(a, req));
    CHECK_EQ(second.h.session_handle, 0x1001u); // 次のハンドルは重複しない
    CHECK_EQ(second.h.status, 0u);

    // 同時に持てるセッション数を超えると NO_MEMORY (応答にペイロードはない)
    for (int i = 2; i < EIP_MAX_SESSIONS; ++i) process(a, req);
    const Bytes full = process(a, req);
    CHECK_EQ(full.size(), 24u);
    const Reply fr = parse(full);
    CHECK_EQ(fr.h.command, (uint16_t)EIP_CMD_REGISTER_SESSION);
    CHECK_EQ(fr.h.status, (uint32_t)EIP_STATUS_NO_MEMORY);
    CHECK_EQ(fr.h.length, 0u);
    CHECK(std::memcmp(fr.h.sender_context, kCtx, 8) == 0);

    // 解除すると空きができて再び登録できる (ハンドルは使い回さない)
    CHECK(process(a, encap(EIP_CMD_UNREGISTER_SESSION, 0x1001, {})).empty());
    const Reply again = parse(process(a, req));
    CHECK_EQ(again.h.status, 0u);
    CHECK_EQ(again.h.session_handle, 0x1000u + EIP_MAX_SESSIONS);
}

TEST_CASE(register_session_validates_the_request_payload) {
    eip_adapter_t a;
    eip_adapter_init(&a, nullptr);
    // サポートしないプロトコルバージョン: 0x69 とサポートする最大バージョンを返す
    const Reply bad = parse(process(a, encap(EIP_CMD_REGISTER_SESSION, 0, {0x02, 0x00, 0x00, 0x00})));
    CHECK_EQ(bad.h.status, (uint32_t)EIP_STATUS_UNSUPPORTED_VER);
    CHECK_EQ(bad.h.session_handle, 0u);
    CHECK_BYTES(bad.payload, 0x01, 0x00, 0x00, 0x00);
    // バージョン 0 も不可
    CHECK_EQ(parse(process(a, encap(EIP_CMD_REGISTER_SESSION, 0, {0x00, 0x00, 0x00, 0x00}))).h.status,
             (uint32_t)EIP_STATUS_UNSUPPORTED_VER);
    // ペイロードが 4 バイトでない
    const Reply shortp = parse(process(a, encap(EIP_CMD_REGISTER_SESSION, 0, {0x01, 0x00})));
    CHECK_EQ(shortp.h.status, (uint32_t)EIP_STATUS_INCORRECT_DATA);
    CHECK_EQ(shortp.h.length, 0u);
    const Reply none = parse(process(a, encap(EIP_CMD_REGISTER_SESSION, 0, {})));
    CHECK_EQ(none.h.status, (uint32_t)EIP_STATUS_INCORRECT_DATA);
    // 失敗した要求ではセッションを消費しない
    const Reply good = parse(process(a, encap(EIP_CMD_REGISTER_SESSION, 0, kRegisterPayload)));
    CHECK_EQ(good.h.session_handle, 0x1000u);
}

TEST_CASE(commands_require_a_registered_session) {
    Dev d;
    const Bytes cip = {0x0E, 0x03, 0x20, 0x01, 0x24, 0x01, 0x30, 0x01};
    // 登録済みのセッション
    CHECK(!cip_of(process(d.a, rr(d.session, cip))).empty());
    // 未登録のハンドル (0, 範囲外, 登録済みの前後)
    for (uint32_t bad : {0u, d.session + 1, d.session - 1, 0xFFFFFFFFu, 0x80000000u}) {
        const Bytes resp = process(d.a, rr(bad, cip));
        CHECK_EQ(resp.size(), 24u);
        const Reply r = parse(resp);
        CHECK_EQ(r.h.command, (uint16_t)EIP_CMD_SEND_RR_DATA);
        CHECK_EQ(r.h.status, (uint32_t)EIP_STATUS_INVALID_SESSION);
        CHECK_EQ(r.h.length, 0u);
        CHECK_EQ(r.h.session_handle, bad);
        CHECK(std::memcmp(r.h.sender_context, kCtx, 8) == 0);
    }
    // 解除後の要求は拒否される (UnRegisterSession には応答がない)
    CHECK(process(d.a, encap(EIP_CMD_UNREGISTER_SESSION, d.session, {})).empty());
    const Reply after = parse(process(d.a, rr(d.session, cip)));
    CHECK_EQ(after.h.status, (uint32_t)EIP_STATUS_INVALID_SESSION);
    // 未登録のセッションの解除は何も起こさない
    Dev e;
    CHECK(process(e.a, encap(EIP_CMD_UNREGISTER_SESSION, 0xDEAD, {})).empty());
    CHECK(!cip_of(process(e.a, rr(e.session, cip))).empty());
}

TEST_CASE(invalid_packets_get_an_error_header) {
    Dev d;
    // 未実装のコマンド
    for (uint16_t cmd : {uint16_t(EIP_CMD_SEND_UNIT_DATA), uint16_t(EIP_CMD_LIST_IDENTITY), uint16_t(EIP_CMD_NOP), uint16_t(0x1234)}) {
        const Bytes resp = process(d.a, encap(cmd, d.session, {1, 2, 3}));
        CHECK_EQ(resp.size(), 24u);
        const Reply r = parse(resp);
        CHECK_EQ(r.h.command, cmd);
        CHECK_EQ(r.h.status, (uint32_t)EIP_STATUS_INVALID_CMD);
        CHECK_EQ(r.h.length, 0u);
        CHECK_EQ(r.h.session_handle, d.session);
    }
    // 自己申告の長さが実際より長い
    Bytes lie = rr(d.session, {0x0E, 0x00});
    lie[2] = (uint8_t)(lie.size() - 24 + 1);
    lie[3] = 0;
    const Reply r = parse(process(d.a, lie));
    CHECK_EQ(r.h.status, (uint32_t)EIP_STATUS_INCORRECT_DATA);
    CHECK_EQ(r.h.length, 0u);
    // 実際より短い申告: 申告した長さまでだけを処理する
    Bytes trailing = rr(d.session, {0x0E, 0x00});
    trailing.insert(trailing.end(), {0xAA, 0xBB, 0xCC});
    CHECK(!cip_of(process(d.a, trailing)).empty());
    // ヘッダに満たない / NULL
    Bytes tiny(23, 0);
    uint8_t out[64];
    CHECK_EQ(eip_adapter_process_packet(&d.a, tiny.data(), tiny.size(), out, sizeof(out)), 0u);
    CHECK_EQ(eip_adapter_process_packet(nullptr, tiny.data(), 24, out, sizeof(out)), 0u);
    CHECK_EQ(eip_adapter_process_packet(&d.a, nullptr, 24, out, sizeof(out)), 0u);
    CHECK_EQ(eip_adapter_process_packet(&d.a, tiny.data(), 24, nullptr, 64), 0u);
    // CPF にデータアイテムがない
    const Bytes nodata = encap(EIP_CMD_SEND_RR_DATA, d.session, cat({u32(0), u16(0), u16(1), u16(0), u16(0)}));
    const Reply nd = parse(process(d.a, nodata));
    CHECK_EQ(nd.h.status, (uint32_t)EIP_STATUS_INCORRECT_DATA);
    CHECK_EQ(nd.h.length, 0u);
}

// ---- Identity オブジェクト ----
TEST_CASE(identity_attributes_are_encoded_little_endian) {
    const cip_identity_attr_t id = make_identity();
    Dev d(&id);
    CHECK_BYTES(get_attr(d, 1, 1, 1), 0x8E, 0x00, 0x00, 0x00, 0x34, 0x12);                   // ベンダ ID
    CHECK_BYTES(get_attr(d, 1, 1, 2), 0x8E, 0x00, 0x00, 0x00, 0x0C, 0x00);                   // 機器タイプ
    CHECK_BYTES(get_attr(d, 1, 1, 3), 0x8E, 0x00, 0x00, 0x00, 0xEF, 0xBE);                   // 製品コード
    CHECK_BYTES(get_attr(d, 1, 1, 4), 0x8E, 0x00, 0x00, 0x00, 0x03, 0x0E);                   // リビジョン (メジャー, マイナー)
    CHECK_BYTES(get_attr(d, 1, 1, 5), 0x8E, 0x00, 0x00, 0x00, 0x60, 0x00);                   // ステータス
    CHECK_BYTES(get_attr(d, 1, 1, 6), 0x8E, 0x00, 0x00, 0x00, 0xD4, 0xC3, 0xB2, 0xA1);       // シリアル
    CHECK_BYTES(get_attr(d, 1, 1, 7), 0x8E, 0x00, 0x00, 0x00, 0x04, 'T', 'e', 's', 't');     // 製品名 (SHORT_STRING)
}

TEST_CASE(identity_defaults_and_long_names) {
    Dev d;
    CHECK_BYTES(get_attr(d, 1, 1, 1), 0x8E, 0x00, 0x00, 0x00, 0x01, 0x00);
    CHECK_BYTES(get_attr(d, 1, 1, 2), 0x8E, 0x00, 0x00, 0x00, 0x0C, 0x00);
    CHECK_BYTES(get_attr(d, 1, 1, 3), 0x8E, 0x00, 0x00, 0x00, 0x01, 0x00);
    CHECK_BYTES(get_attr(d, 1, 1, 4), 0x8E, 0x00, 0x00, 0x00, 0x01, 0x00);
    CHECK_BYTES(get_attr(d, 1, 1, 5), 0x8E, 0x00, 0x00, 0x00, 0x60, 0x00);
    CHECK_BYTES(get_attr(d, 1, 1, 6), 0x8E, 0x00, 0x00, 0x00, 0x78, 0x56, 0x34, 0x12);
    const Bytes name = get_attr(d, 1, 1, 7);
    CHECK_EQ(name[4], 20);
    CHECK(std::string(name.begin() + 5, name.end()) == "Embedded EIP Adapter");

    // 終端のない 32 文字の名前でも、構造体の外を読まない
    cip_identity_attr_t id = make_identity();
    std::memset(id.product_name, 'x', sizeof(id.product_name));
    Dev e(&id);
    const Bytes long_name = get_attr(e, 1, 1, 7);
    REQUIRE(long_name.size() == 4u + 1 + 32);
    CHECK_EQ(long_name[4], 32);
}

TEST_CASE(identity_status_codes) {
    const cip_identity_attr_t id = make_identity();
    Dev d(&id);
    CHECK_BYTES(get_attr(d, 1, 1, 0), 0x8E, 0x00, 0x14, 0x00); // 属性なし
    CHECK_BYTES(get_attr(d, 1, 1, 8), 0x8E, 0x00, 0x14, 0x00);
    CHECK_BYTES(get_attr(d, 1, 1, 0x100), 0x8E, 0x00, 0x14, 0x00);
    CHECK_BYTES(get_attr(d, 1, 2, 1), 0x8E, 0x00, 0x05, 0x00); // インスタンスが存在しない
    CHECK_BYTES(get_attr(d, 1, 0x100, 1), 0x8E, 0x00, 0x05, 0x00);
    CHECK_BYTES(get_attr(d, 0x99, 1, 1), 0x8E, 0x00, 0x05, 0x00); // クラスが存在しない
    CHECK_BYTES(get_attr(d, 0x0123, 1, 1), 0x8E, 0x00, 0x05, 0x00);
    CHECK_BYTES(d.cip({0x05, 0x02, 0x20, 0x01, 0x24, 0x01}), 0x85, 0x00, 0x08, 0x00); // Reset 未対応
    // Identity の属性は読み取り専用
    CHECK_BYTES(d.cip({0x10, 0x03, 0x20, 0x01, 0x24, 0x01, 0x30, 0x01, 0x01, 0x00}), 0x90, 0x00, 0x0E, 0x00);
    CHECK_BYTES(d.cip({0x10, 0x03, 0x20, 0x01, 0x24, 0x01, 0x30, 0x63, 0x01, 0x00}), 0x90, 0x00, 0x14, 0x00);
    // 壊れた CIP 要求: パスの大きさが不正
    CHECK_BYTES(d.cip({0x0E}), 0x8E, 0x00, 0x26, 0x00);
    CHECK_BYTES(d.cip({0x0E, 0x05, 0x20, 0x01}), 0x8E, 0x00, 0x26, 0x00);
}

// ---- Assembly オブジェクト ----
TEST_CASE(assembly_api_limits) {
    eip_adapter_t a;
    eip_adapter_init(&a, nullptr);
    CHECK(!eip_adapter_add_assembly(&a, 100, EIP_MAX_ASSEMBLY_SIZE + 1));
    CHECK(eip_adapter_add_assembly(&a, 100, EIP_MAX_ASSEMBLY_SIZE));
    CHECK(!eip_adapter_add_assembly(&a, 100, 4)); // 同じインスタンス ID の二重登録は不可
    for (int i = 1; i < EIP_MAX_ASSEMBLIES; ++i) CHECK(eip_adapter_add_assembly(&a, (uint16_t)(100 + i), 4));
    CHECK(!eip_adapter_add_assembly(&a, 200, 4)); // 満杯
    CHECK(!eip_adapter_add_assembly(nullptr, 1, 4));

    const uint8_t src[] = {1, 2, 3, 4, 5, 6};
    uint8_t dst[EIP_MAX_ASSEMBLY_SIZE];
    size_t sz = 0;
    CHECK(eip_adapter_set_assembly_data(&a, 101, src, 6)); // 4 バイトに切り詰める
    CHECK(eip_adapter_get_assembly_data(&a, 101, dst, &sz));
    CHECK_EQ(sz, 4u);
    CHECK(std::memcmp(dst, src, 4) == 0);
    CHECK(eip_adapter_set_assembly_data(&a, 101, src, 2)); // 短い書き込みは先頭だけ上書き
    const uint8_t ff[] = {9, 9};
    eip_adapter_set_assembly_data(&a, 101, ff, 2);
    eip_adapter_get_assembly_data(&a, 101, dst, &sz);
    CHECK_EQ(dst[0], 9);
    CHECK_EQ(dst[1], 9);
    CHECK_EQ(dst[2], 3);
    CHECK(!eip_adapter_set_assembly_data(&a, 999, src, 2));
    CHECK(!eip_adapter_get_assembly_data(&a, 999, dst, &sz));
    CHECK(!eip_adapter_set_assembly_data(&a, 101, nullptr, 2));
    CHECK(!eip_adapter_set_assembly_data(nullptr, 101, src, 2));
    CHECK(!eip_adapter_get_assembly_data(&a, 101, nullptr, &sz));
    CHECK(!eip_adapter_get_assembly_data(&a, 101, dst, nullptr));
    CHECK(!eip_adapter_get_assembly_data(nullptr, 101, dst, &sz));
}

TEST_CASE(assembly_get_and_set_through_cip) {
    Dev d;
    REQUIRE(eip_adapter_add_assembly(&d.a, 100, 4));
    const uint8_t init[] = {0x10, 0x20, 0x30, 0x40};
    eip_adapter_set_assembly_data(&d.a, 100, init, 4);
    CHECK_BYTES(get_attr(d, 4, 100, 3), 0x8E, 0x00, 0x00, 0x00, 0x10, 0x20, 0x30, 0x40);

    uint8_t buf[64];
    const uint8_t w[] = {1, 2, 3, 4};
    size_t n = cip_build_set_attribute_single(4, 100, 3, w, 4, buf, sizeof(buf));
    CHECK_BYTES(d.cip(Bytes(buf, buf + n)), 0x90, 0x00, 0x00, 0x00);
    uint8_t out[8];
    size_t sz = 0;
    eip_adapter_get_assembly_data(&d.a, 100, out, &sz);
    CHECK(std::memcmp(out, w, 4) == 0);

    // データ長の不一致 (不足 0x13 / 過剰 0x15) では、アセンブリを書き換えない
    const uint8_t w3[] = {9, 9, 9}, w5[] = {8, 8, 8, 8, 8};
    n = cip_build_set_attribute_single(4, 100, 3, w3, 3, buf, sizeof(buf));
    CHECK_BYTES(d.cip(Bytes(buf, buf + n)), 0x90, 0x00, 0x13, 0x00);
    n = cip_build_set_attribute_single(4, 100, 3, w5, 5, buf, sizeof(buf));
    CHECK_BYTES(d.cip(Bytes(buf, buf + n)), 0x90, 0x00, 0x15, 0x00);
    n = cip_build_set_attribute_single(4, 100, 3, nullptr, 0, buf, sizeof(buf));
    CHECK_BYTES(d.cip(Bytes(buf, buf + n)), 0x90, 0x00, 0x13, 0x00);
    eip_adapter_get_assembly_data(&d.a, 100, out, &sz);
    CHECK(std::memcmp(out, w, 4) == 0);

    // 状態コード
    CHECK_BYTES(get_attr(d, 4, 100, 4), 0x8E, 0x00, 0x14, 0x00);   // 未対応の属性
    CHECK_BYTES(get_attr(d, 4, 100, 1), 0x8E, 0x00, 0x14, 0x00);
    n = cip_build_set_attribute_single(4, 100, 4, w, 4, buf, sizeof(buf));
    CHECK_BYTES(d.cip(Bytes(buf, buf + n)), 0x90, 0x00, 0x14, 0x00);
    CHECK_BYTES(get_attr(d, 4, 101, 3), 0x8E, 0x00, 0x05, 0x00);   // インスタンスなし
    CHECK_BYTES(d.cip({0x05, 0x03, 0x20, 0x04, 0x24, 0x64, 0x30, 0x03}), 0x85, 0x00, 0x08, 0x00); // 未対応のサービス
}

TEST_CASE(responses_echo_session_and_context_and_fit_the_buffer) {
    Dev d;
    const Bytes req = rr(d.session, {0x0E, 0x03, 0x20, 0x01, 0x24, 0x01, 0x30, 0x01});
    const Bytes resp = process(d.a, req);
    const Reply r = parse(resp);
    CHECK_EQ(r.h.command, (uint16_t)EIP_CMD_SEND_RR_DATA);
    CHECK_EQ(r.h.session_handle, d.session);
    CHECK_EQ(r.h.status, 0u);
    CHECK(std::memcmp(r.h.sender_context, kCtx, 8) == 0);
    CHECK_EQ(r.h.length, r.payload.size());
    CHECK_EQ(r.h.length, 16u + 6); // CPF 16 + CIP 応答 (4 + ベンダ ID 2)
    // 応答が出力バッファに収まらなければ何も返さない
    CHECK_EQ(process(d.a, req, resp.size()).size(), resp.size());
    CHECK_EQ(process(d.a, req, resp.size() - 1).size(), 0u);
}
TEST_CASE(output_buffer_smaller_than_a_header_is_never_written) {
    Dev d;
    uint8_t small[64];
    std::memset(small, 0xEE, sizeof(small));
    const Bytes bad = encap(0x1234, d.session, {}); // 未実装のコマンドは、ヘッダだけのエラー応答になる
    CHECK_EQ(eip_adapter_process_packet(&d.a, bad.data(), bad.size(), small, 23), 0u);
    CHECK_EQ(small[0], 0xEE);
    CHECK_EQ(eip_adapter_process_packet(&d.a, bad.data(), bad.size(), small, 24), 24u);
    CHECK_EQ(small[24], 0xEE);
}

TEST_CASE(boundary_cases_for_parsing) {
    // プロトコルバージョンは 16 ビット全体を見る
    eip_adapter_t a;
    eip_adapter_init(&a, nullptr);
    CHECK_EQ(parse(process(a, encap(EIP_CMD_REGISTER_SESSION, 0, {0x01, 0x01, 0x00, 0x00}))).h.status,
             (uint32_t)EIP_STATUS_UNSUPPORTED_VER);

    // 長さ 0 のデータアイテムだけの CPF (最小の 16 バイト) も解析できる
    const uint8_t *d = nullptr;
    size_t n = 99;
    const Bytes empty = cpf({});
    CHECK_EQ(empty.size(), 16u);
    // cpf() は CIP データが空のとき 0 長のアイテムを作る
    CHECK(eip_parse_send_rr_data(empty.data(), empty.size(), &d, &n));
    CHECK_EQ(n, 0u);

    // 16 ビットのセグメントが末尾にあっても読める (クラス / インスタンス / 属性のどれが最後でも)
    cip_epath_t p;
    const uint8_t cls_last[] = {0x24, 0x01, 0x30, 0x01, 0x21, 0x00, 0x02, 0x01};
    CHECK_EQ(cip_decode_epath(cls_last, sizeof(cls_last), &p), 8u);
    CHECK_EQ(p.class_id, 0x0102u);
    const uint8_t inst_last[] = {0x20, 0x01, 0x30, 0x01, 0x25, 0x00, 0x04, 0x03};
    CHECK_EQ(cip_decode_epath(inst_last, sizeof(inst_last), &p), 8u);
    CHECK_EQ(p.instance_id, 0x0304u);
    const uint8_t attr_last[] = {0x20, 0x01, 0x24, 0x01, 0x31, 0x00, 0x06, 0x05};
    CHECK_EQ(cip_decode_epath(attr_last, sizeof(attr_last), &p), 8u);
    CHECK_EQ(p.attribute_id, 0x0506u);
    // 8 ビットのセグメントが末尾の場合も同様
    const uint8_t cls8_last[] = {0x24, 0x01, 0x30, 0x01, 0x20, 0x07};
    CHECK_EQ(cip_decode_epath(cls8_last, sizeof(cls8_last), &p), 6u);
    CHECK_EQ(p.class_id, 7u);
    const uint8_t inst8_last[] = {0x20, 0x01, 0x30, 0x01, 0x24, 0x09};
    CHECK_EQ(cip_decode_epath(inst8_last, sizeof(inst8_last), &p), 6u);
    CHECK_EQ(p.instance_id, 9u);
}

TEST_CASE(parse_does_not_read_items_beyond_the_payload_length) {
    // アイテム数 2 だが、2 つ目のヘッダはペイロードの外 (その先のバイトが有効なデータアイテムでも読まない)
    const Bytes buf = cat({u32(0), u16(0), u16(2), u16(0x8000), u16(4), Bytes{1, 2, 3, 4}, // ここまでで 16 バイト
                           u16(0x00B2), u16(1), Bytes{0x77}});
    const uint8_t *d = nullptr;
    size_t n = 0;
    CHECK(!eip_parse_send_rr_data(buf.data(), 16, &d, &n));
    CHECK(eip_parse_send_rr_data(buf.data(), buf.size(), &d, &n)); // 全体を渡せば読める
    CHECK_EQ(d[0], 0x77);
}

EMBED_TEST_MAIN()
