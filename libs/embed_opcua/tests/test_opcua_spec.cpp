// OPC UA Part 6 (Mapping) の UA Binary 符号化と TCP トランスポートのバイト列 (リトルエンディアン)、
// NodeId の符号化形式、Variant / DataValue の符号化マスク、Hello / Acknowledge / Error のヘッダ、
// 読み書きサービスの StatusCode を固定するテスト。
#include "embed_test/embed_test.hpp"
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

#include "embed_opcua/opcua_binary.h"
#include "embed_opcua/opcua_server.h"
#include "embed_opcua/opcua_types.h"

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
Bytes u32(uint32_t v) { return {uint8_t(v), uint8_t(v >> 8), uint8_t(v >> 16), uint8_t(v >> 24)}; }
Bytes str(const std::string &s) { return cat({u32((uint32_t)s.size()), Bytes(s.begin(), s.end())}); }

opcua_variant_t var(opcua_type_id_t t) {
    opcua_variant_t v;
    std::memset(&v, 0, sizeof(v));
    v.type = t;
    return v;
}

Bytes enc_variant(const opcua_variant_t &v, size_t cap = 64, size_t *n_out = nullptr) {
    Bytes b(cap, 0xEE);
    const size_t n = opcua_encode_variant(&v, b.data(), b.size());
    if (n_out) *n_out = n;
    b.resize(n);
    return b;
}

opcua_nodeid_t num_id(uint16_t ns, uint32_t id) {
    opcua_nodeid_t n;
    std::memset(&n, 0, sizeof(n));
    n.namespace_index = ns;
    n.id_type = OPCUA_NODEID_NUMERIC;
    n.id.numeric = id;
    return n;
}
opcua_nodeid_t str_id(uint16_t ns, const char *s) {
    opcua_nodeid_t n;
    std::memset(&n, 0, sizeof(n));
    n.namespace_index = ns;
    n.id_type = OPCUA_NODEID_STRING;
    std::snprintf(n.id.string, sizeof(n.id.string), "%s", s);
    return n;
}
Bytes enc_nodeid(const opcua_nodeid_t &n) {
    Bytes b(32, 0xEE);
    b.resize(opcua_encode_nodeid(&n, b.data(), b.size()));
    return b;
}

opcua_variant_t int32_var(int32_t v) {
    opcua_variant_t x = var(OPCUA_TYPE_INT32);
    x.value.int32_val = v;
    return x;
}
} // namespace

// ---- プリミティブ型 (リトルエンディアン) ----
TEST_CASE(primitive_encodings_are_little_endian) {
    uint8_t b[16];
    std::memset(b, 0xEE, sizeof(b));
    CHECK_EQ(opcua_encode_u8(0xAB, b, 16), 1u);
    CHECK_EQ(b[0], 0xAB);
    CHECK_EQ(b[1], 0xEE);
    CHECK_EQ(opcua_encode_u16(0x1234, b, 16), 2u);
    CHECK_BYTES(Bytes(b, b + 3), 0x34, 0x12, 0xEE);
    CHECK_EQ(opcua_encode_u32(0x11223344, b, 16), 4u);
    CHECK_BYTES(Bytes(b, b + 5), 0x44, 0x33, 0x22, 0x11, 0xEE);
    CHECK_EQ(opcua_encode_u64(0x0102030405060708ULL, b, 16), 8u);
    CHECK_BYTES(Bytes(b, b + 9), 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0xEE);
    CHECK_EQ(opcua_encode_float(1.0f, b, 16), 4u);
    CHECK_BYTES(Bytes(b, b + 4), 0x00, 0x00, 0x80, 0x3F);
    CHECK_EQ(opcua_encode_float(-2.5f, b, 16), 4u);
    CHECK_BYTES(Bytes(b, b + 4), 0x00, 0x00, 0x20, 0xC0);
    CHECK_EQ(opcua_encode_double(1.0, b, 16), 8u);
    CHECK_BYTES(Bytes(b, b + 8), 0, 0, 0, 0, 0, 0, 0xF0, 0x3F);
    CHECK_EQ(opcua_encode_double(0.1, b, 16), 8u);
    CHECK_BYTES(Bytes(b, b + 8), 0x9A, 0x99, 0x99, 0x99, 0x99, 0x99, 0xB9, 0x3F);
    CHECK_EQ(opcua_encode_double(-2.0, b, 16), 8u);
    CHECK_BYTES(Bytes(b, b + 8), 0, 0, 0, 0, 0, 0, 0, 0xC0);
}

TEST_CASE(primitive_capacity_boundaries) {
    uint8_t b[16];
    CHECK_EQ(opcua_encode_u8(1, b, 0), 0u);
    CHECK_EQ(opcua_encode_u8(1, b, 1), 1u);
    CHECK_EQ(opcua_encode_u16(1, b, 1), 0u);
    CHECK_EQ(opcua_encode_u16(1, b, 2), 2u);
    CHECK_EQ(opcua_encode_u32(1, b, 3), 0u);
    CHECK_EQ(opcua_encode_u32(1, b, 4), 4u);
    CHECK_EQ(opcua_encode_u64(1, b, 7), 0u);
    CHECK_EQ(opcua_encode_u64(1, b, 8), 8u);
    CHECK_EQ(opcua_encode_float(1, b, 3), 0u);
    CHECK_EQ(opcua_encode_float(1, b, 4), 4u);
    CHECK_EQ(opcua_encode_double(1, b, 7), 0u);
    CHECK_EQ(opcua_encode_double(1, b, 8), 8u);
    CHECK_EQ(opcua_encode_u32(1, nullptr, 8), 0u);
    CHECK_EQ(opcua_encode_double(1, nullptr, 8), 0u);

    uint8_t v8 = 0;
    uint16_t v16 = 0;
    uint32_t v32 = 0;
    uint64_t v64 = 0;
    float f = 0;
    double d = 0;
    const uint8_t src[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    CHECK_EQ(opcua_decode_u8(src, 0, &v8), 0u);
    CHECK_EQ(opcua_decode_u8(src, 1, &v8), 1u);
    CHECK_EQ(opcua_decode_u16(src, 1, &v16), 0u);
    CHECK_EQ(opcua_decode_u16(src, 2, &v16), 2u);
    CHECK_EQ(v16, 0x0201u);
    CHECK_EQ(opcua_decode_u32(src, 3, &v32), 0u);
    CHECK_EQ(opcua_decode_u32(src, 4, &v32), 4u);
    CHECK_EQ(v32, 0x04030201u);
    CHECK_EQ(opcua_decode_u64(src, 7, &v64), 0u);
    CHECK_EQ(opcua_decode_u64(src, 8, &v64), 8u);
    CHECK_EQ(v64, 0x0807060504030201ULL);
    CHECK_EQ(opcua_decode_float(src, 3, &f), 0u);
    CHECK_EQ(opcua_decode_float(src, 4, &f), 4u);
    CHECK_EQ(opcua_decode_double(src, 7, &d), 0u);
    CHECK_EQ(opcua_decode_double(src, 8, &d), 8u);
    CHECK_EQ(opcua_decode_u8(nullptr, 1, &v8), 0u);
    CHECK_EQ(opcua_decode_u8(src, 1, nullptr), 0u);
    CHECK_EQ(opcua_decode_u32(src, 4, nullptr), 0u);
    CHECK_EQ(opcua_decode_float(src, 4, nullptr), 0u);
    CHECK_EQ(opcua_decode_double(src, 8, nullptr), 0u);

    // 上位ビットの立ったバイト
    const uint8_t hi[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x80};
    opcua_decode_u32(hi, 4, &v32);
    CHECK_EQ(v32, 0xFFFFFFFFu);
    opcua_decode_u64(hi, 8, &v64);
    CHECK_EQ(v64, 0x80FFFFFFFFFFFFFFULL);
    opcua_decode_u16(hi, 2, &v16);
    CHECK_EQ(v16, 0xFFFFu);
}

TEST_CASE(string_encoding_uses_int32_length_with_minus_one_for_null) {
    uint8_t b[32];
    std::memset(b, 0xEE, sizeof(b));
    CHECK_EQ(opcua_encode_string("abc", b, sizeof(b)), 7u);
    CHECK_BYTES(Bytes(b, b + 8), 0x03, 0x00, 0x00, 0x00, 'a', 'b', 'c', 0xEE);
    CHECK_EQ(opcua_encode_string("", b, sizeof(b)), 4u);
    CHECK_BYTES(Bytes(b, b + 4), 0x00, 0x00, 0x00, 0x00);
    CHECK_EQ(opcua_encode_string(nullptr, b, sizeof(b)), 4u);
    CHECK_BYTES(Bytes(b, b + 4), 0xFF, 0xFF, 0xFF, 0xFF);
    // 容量の境界
    CHECK_EQ(opcua_encode_string("abc", b, 6), 0u);
    CHECK_EQ(opcua_encode_string("abc", b, 7), 7u);
    CHECK_EQ(opcua_encode_string("abc", b, 3), 0u);
    CHECK_EQ(opcua_encode_string("", b, 4), 4u);
    CHECK_EQ(opcua_encode_string("", b, 3), 0u);
    CHECK_EQ(opcua_encode_string("abc", nullptr, 32), 0u);

    // 復号: 長さ 0 と -1 は空文字列
    char out[8];
    const Bytes abc = str("abc");
    CHECK_EQ(opcua_decode_string(abc.data(), abc.size(), out, sizeof(out)), 7u);
    CHECK(std::string(out) == "abc");
    const Bytes empty = str("");
    std::memset(out, 'x', sizeof(out));
    CHECK_EQ(opcua_decode_string(empty.data(), empty.size(), out, sizeof(out)), 4u);
    CHECK_EQ(out[0], 0);
    const Bytes null = u32(0xFFFFFFFFu);
    std::memset(out, 'x', sizeof(out));
    CHECK_EQ(opcua_decode_string(null.data(), null.size(), out, sizeof(out)), 4u);
    CHECK_EQ(out[0], 0);
    // 出力先に終端まで収まらない / 入力が足りない / 負の長さ
    CHECK_EQ(opcua_decode_string(abc.data(), abc.size(), out, 3), 0u);
    CHECK_EQ(opcua_decode_string(abc.data(), abc.size(), out, 4), 7u);
    CHECK_EQ(opcua_decode_string(abc.data(), abc.size() - 1, out, sizeof(out)), 0u);
    CHECK_EQ(opcua_decode_string(abc.data(), 3, out, sizeof(out)), 0u);
    const Bytes neg = u32(0x80000000u);
    CHECK_EQ(opcua_decode_string(neg.data(), neg.size(), out, sizeof(out)), 0u);
    CHECK_EQ(opcua_decode_string(abc.data(), abc.size(), nullptr, 8), 0u);
    CHECK_EQ(opcua_decode_string(abc.data(), abc.size(), out, 0), 0u);
    CHECK_EQ(opcua_decode_string(nullptr, 8, out, 8), 0u);
}

// ---- Variant (符号化マスク = 組み込み型 ID) ----
TEST_CASE(variant_scalar_bytes) {
    opcua_variant_t v = var(OPCUA_TYPE_BOOLEAN);
    v.value.boolean = true;
    CHECK_BYTES(enc_variant(v), 0x01, 0x01);
    v.value.boolean = false;
    CHECK_BYTES(enc_variant(v), 0x01, 0x00);
    v = var(OPCUA_TYPE_SBYTE);
    v.value.sbyte_val = -1;
    CHECK_BYTES(enc_variant(v), 0x02, 0xFF);
    v = var(OPCUA_TYPE_BYTE);
    v.value.byte_val = 0xFE;
    CHECK_BYTES(enc_variant(v), 0x03, 0xFE);
    v = var(OPCUA_TYPE_INT16);
    v.value.int16_val = -2;
    CHECK_BYTES(enc_variant(v), 0x04, 0xFE, 0xFF);
    v = var(OPCUA_TYPE_UINT16);
    v.value.uint16_val = 0xABCD;
    CHECK_BYTES(enc_variant(v), 0x05, 0xCD, 0xAB);
    v = var(OPCUA_TYPE_INT32);
    v.value.int32_val = -2;
    CHECK_BYTES(enc_variant(v), 0x06, 0xFE, 0xFF, 0xFF, 0xFF);
    v = var(OPCUA_TYPE_UINT32);
    v.value.uint32_val = 0xDEADBEEF;
    CHECK_BYTES(enc_variant(v), 0x07, 0xEF, 0xBE, 0xAD, 0xDE);
    v = var(OPCUA_TYPE_INT64);
    v.value.int64_val = -2;
    CHECK_BYTES(enc_variant(v), 0x08, 0xFE, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF);
    v = var(OPCUA_TYPE_UINT64);
    v.value.uint64_val = 0x0102030405060708ULL;
    CHECK_BYTES(enc_variant(v), 0x09, 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01);
    v = var(OPCUA_TYPE_FLOAT);
    v.value.float_val = 1.0f;
    CHECK_BYTES(enc_variant(v), 0x0A, 0x00, 0x00, 0x80, 0x3F);
    v = var(OPCUA_TYPE_DOUBLE);
    v.value.double_val = 1.0;
    CHECK_BYTES(enc_variant(v), 0x0B, 0, 0, 0, 0, 0, 0, 0xF0, 0x3F);
    v = var(OPCUA_TYPE_STRING);
    std::snprintf(v.value.string_val, sizeof(v.value.string_val), "abc");
    CHECK_BYTES(enc_variant(v), 0x0C, 0x03, 0x00, 0x00, 0x00, 'a', 'b', 'c');
    v = var(OPCUA_TYPE_DATETIME);
    v.value.uint64_val = 0x01D0B0C0D0E0F000ULL;
    CHECK_BYTES(enc_variant(v), 0x0D, 0x00, 0xF0, 0xE0, 0xD0, 0xC0, 0xB0, 0xD0, 0x01);
    v = var(OPCUA_TYPE_STATUSCODE);
    v.value.status_val = OPCUA_STATUS_BAD_NODE_ID_UNKNOWN;
    CHECK_BYTES(enc_variant(v), 0x13, 0x00, 0x00, 0x34, 0x80);
    CHECK_BYTES(enc_variant(var(OPCUA_TYPE_NULL)), 0x00);
}

TEST_CASE(variant_decoding_and_errors) {
    opcua_variant_t v;
    auto dec = [&](Bytes b) { return opcua_decode_variant(b.data(), b.size(), &v); };
    CHECK_EQ(dec({0x01, 0x01}), 2u);
    CHECK_EQ(v.type, OPCUA_TYPE_BOOLEAN);
    CHECK(v.value.boolean);
    CHECK_EQ(dec({0x01, 0x07}), 2u); // 0 以外は true
    CHECK(v.value.boolean);
    CHECK_EQ(dec({0x02, 0x80}), 2u);
    CHECK_EQ(v.value.sbyte_val, -128);
    CHECK_EQ(dec({0x04, 0x00, 0x80}), 3u);
    CHECK_EQ(v.value.int16_val, -32768);
    CHECK_EQ(dec({0x06, 0xFF, 0xFF, 0xFF, 0x7F}), 5u);
    CHECK_EQ(v.value.int32_val, 2147483647);
    CHECK_EQ(dec({0x0A, 0x00, 0x00, 0x20, 0xC0}), 5u);
    CHECK_EQ(v.value.float_val, -2.5f);
    CHECK_EQ(dec({0x0B, 0, 0, 0, 0, 0, 0, 0, 0xC0}), 9u);
    CHECK_EQ(v.value.double_val, -2.0);
    CHECK_EQ(dec({0x0C, 0x02, 0x00, 0x00, 0x00, 'o', 'k'}), 7u);
    CHECK(std::string(v.value.string_val) == "ok");
    CHECK_EQ(dec({0x13, 0x00, 0x00, 0x35, 0x80}), 5u);
    CHECK_EQ(v.value.status_val, OPCUA_STATUS_BAD_ATTRIBUTE_ID_INVALID);
    CHECK_EQ(dec({0x00}), 1u);
    CHECK_EQ(v.type, OPCUA_TYPE_NULL);
    // 1 バイト足りない各型
    CHECK_EQ(dec({0x01}), 0u);
    CHECK_EQ(dec({0x02}), 0u);
    CHECK_EQ(dec({0x04, 0x00}), 0u);
    CHECK_EQ(dec({0x06, 0x00, 0x00, 0x00}), 0u);
    CHECK_EQ(dec({0x08, 0, 0, 0, 0, 0, 0, 0}), 0u);
    CHECK_EQ(dec({0x0A, 0, 0, 0}), 0u);
    CHECK_EQ(dec({0x0B, 0, 0, 0, 0, 0, 0, 0}), 0u);
    CHECK_EQ(dec({0x0C, 0x02, 0x00, 0x00, 0x00, 'o'}), 0u);
    CHECK_EQ(dec({0x0D, 0, 0, 0, 0, 0, 0, 0}), 0u);
    CHECK_EQ(dec({0x13, 0, 0, 0}), 0u);
    // 配列 (0x80) / 次元 (0x40) / 未対応の型は拒否する
    CHECK_EQ(dec({0x86, 0x01, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00}), 0u);
    CHECK_EQ(dec({0x46, 0x05, 0x00, 0x00, 0x00}), 0u);
    CHECK_EQ(dec({0x0E, 0x00}), 0u); // Guid
    CHECK_EQ(dec({0x0F, 0x00}), 0u); // ByteString
    CHECK_EQ(dec({0x14, 0x00}), 0u);
    CHECK_EQ(dec({}), 0u);
    CHECK_EQ(opcua_decode_variant(nullptr, 4, &v), 0u);
    const uint8_t one[] = {0x00};
    CHECK_EQ(opcua_decode_variant(one, 1, nullptr), 0u);
    // 符号化できない型・NULL 引数
    uint8_t buf[16];
    opcua_variant_t bad = var((opcua_type_id_t)99);
    CHECK_EQ(opcua_encode_variant(&bad, buf, sizeof(buf)), 0u);
    CHECK_EQ(opcua_encode_variant(nullptr, buf, sizeof(buf)), 0u);
    CHECK_EQ(opcua_encode_variant(&bad, nullptr, sizeof(buf)), 0u);
}

TEST_CASE(variant_encoding_respects_capacity_for_every_type) {
    struct Case {
        opcua_type_id_t type;
        size_t total; // 型 ID 1 バイト + 本体
    };
    const Case cases[] = {{OPCUA_TYPE_BOOLEAN, 2}, {OPCUA_TYPE_SBYTE, 2},  {OPCUA_TYPE_BYTE, 2},
                          {OPCUA_TYPE_INT16, 3},   {OPCUA_TYPE_UINT16, 3}, {OPCUA_TYPE_INT32, 5},
                          {OPCUA_TYPE_UINT32, 5},  {OPCUA_TYPE_INT64, 9},  {OPCUA_TYPE_UINT64, 9},
                          {OPCUA_TYPE_FLOAT, 5},   {OPCUA_TYPE_DOUBLE, 9}, {OPCUA_TYPE_DATETIME, 9},
                          {OPCUA_TYPE_STATUSCODE, 5}, {OPCUA_TYPE_STRING, 5}};
    for (const Case &c : cases) {
        opcua_variant_t v = var(c.type);
        uint8_t buf[16];
        size_t n = 0;
        enc_variant(v, 16, &n);
        CHECK_EQ(n, c.total);
        CHECK_EQ(opcua_encode_variant(&v, buf, c.total), c.total);
        CHECK_EQ(opcua_encode_variant(&v, buf, c.total - 1), 0u);
    }
    // 終端のない文字列は符号化しない
    opcua_variant_t s = var(OPCUA_TYPE_STRING);
    std::memset(s.value.string_val, 'x', sizeof(s.value.string_val));
    uint8_t big[128];
    CHECK_EQ(opcua_encode_variant(&s, big, sizeof(big)), 0u);
}

// ---- NodeId ----
TEST_CASE(nodeid_encoding_formats_and_boundaries) {
    CHECK_BYTES(enc_nodeid(num_id(0, 0)), 0x00, 0x00);                             // two-byte
    CHECK_BYTES(enc_nodeid(num_id(0, 255)), 0x00, 0xFF);                           // two-byte の最大
    CHECK_BYTES(enc_nodeid(num_id(0, 256)), 0x01, 0x00, 0x00, 0x01);               // four-byte
    CHECK_BYTES(enc_nodeid(num_id(1, 0)), 0x01, 0x01, 0x00, 0x00);                 // 名前空間 1 は four-byte
    CHECK_BYTES(enc_nodeid(num_id(1, 255)), 0x01, 0x01, 0xFF, 0x00);
    CHECK_BYTES(enc_nodeid(num_id(255, 65535)), 0x01, 0xFF, 0xFF, 0xFF);           // four-byte の最大
    CHECK_BYTES(enc_nodeid(num_id(256, 1)), 0x02, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00); // numeric
    CHECK_BYTES(enc_nodeid(num_id(0, 65536)), 0x02, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00);
    CHECK_BYTES(enc_nodeid(num_id(1, 0xFFFFFFFFu)), 0x02, 0x01, 0x00, 0xFF, 0xFF, 0xFF, 0xFF);
    CHECK_BYTES(enc_nodeid(num_id(0xFFFF, 0x12345678)), 0x02, 0xFF, 0xFF, 0x78, 0x56, 0x34, 0x12);
    CHECK_BYTES(enc_nodeid(str_id(2, "Temp")), 0x03, 0x02, 0x00, 0x04, 0x00, 0x00, 0x00, 'T', 'e', 'm', 'p');
    CHECK_BYTES(enc_nodeid(str_id(0, "")), 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);
}

TEST_CASE(nodeid_encoding_capacity_and_invalid_input) {
    uint8_t b[32];
    opcua_nodeid_t two = num_id(0, 7), four = num_id(2, 300), full = num_id(300, 70000), s = str_id(1, "ab");
    CHECK_EQ(opcua_encode_nodeid(&two, b, 1), 0u);
    CHECK_EQ(opcua_encode_nodeid(&two, b, 2), 2u);
    CHECK_EQ(opcua_encode_nodeid(&four, b, 3), 0u);
    CHECK_EQ(opcua_encode_nodeid(&four, b, 4), 4u);
    CHECK_EQ(opcua_encode_nodeid(&full, b, 6), 0u);
    CHECK_EQ(opcua_encode_nodeid(&full, b, 7), 7u);
    CHECK_EQ(opcua_encode_nodeid(&s, b, 8), 0u);
    CHECK_EQ(opcua_encode_nodeid(&s, b, 9), 9u);
    CHECK_EQ(opcua_encode_nodeid(&s, b, 6), 0u);
    CHECK_EQ(opcua_encode_nodeid(nullptr, b, 32), 0u);
    CHECK_EQ(opcua_encode_nodeid(&two, nullptr, 32), 0u);
    CHECK_EQ(opcua_encode_nodeid(&two, b, 0), 0u);
    opcua_nodeid_t bad = str_id(0, "x");
    bad.id_type = (opcua_nodeid_type_t)7;
    CHECK_EQ(opcua_encode_nodeid(&bad, b, 32), 0u);
    opcua_nodeid_t unterminated = str_id(0, "x");
    std::memset(unterminated.id.string, 'z', sizeof(unterminated.id.string));
    CHECK_EQ(opcua_encode_nodeid(&unterminated, b, 128), 0u);
}

TEST_CASE(nodeid_decoding) {
    opcua_nodeid_t n;
    auto dec = [&](Bytes b) { return opcua_decode_nodeid(b.data(), b.size(), &n); };
    CHECK_EQ(dec({0x00, 0x2A}), 2u);
    CHECK_EQ(n.namespace_index, 0u);
    CHECK_EQ(n.id_type, OPCUA_NODEID_NUMERIC);
    CHECK_EQ(n.id.numeric, 42u);
    CHECK_EQ(dec({0x01, 0x05, 0x34, 0x12}), 4u);
    CHECK_EQ(n.namespace_index, 5u);
    CHECK_EQ(n.id.numeric, 0x1234u);
    CHECK_EQ(dec({0x02, 0x01, 0x01, 0x78, 0x56, 0x34, 0x12}), 7u);
    CHECK_EQ(n.namespace_index, 0x0101u);
    CHECK_EQ(n.id.numeric, 0x12345678u);
    CHECK_EQ(dec({0x03, 0x02, 0x00, 0x02, 0x00, 0x00, 0x00, 'h', 'i'}), 9u);
    CHECK_EQ(n.namespace_index, 2u);
    CHECK_EQ(n.id_type, OPCUA_NODEID_STRING);
    CHECK(std::string(n.id.string) == "hi");
    // 切り詰め (各形式で 1 バイト不足)
    CHECK_EQ(dec({0x00}), 0u);
    CHECK_EQ(dec({0x01, 0x05, 0x34}), 0u);
    CHECK_EQ(dec({0x01, 0x05}), 0u);
    CHECK_EQ(dec({0x02, 0x01, 0x01, 0x78, 0x56, 0x34}), 0u);
    CHECK_EQ(dec({0x02, 0x01}), 0u);
    CHECK_EQ(dec({0x03, 0x02}), 0u);
    CHECK_EQ(dec({0x03, 0x02, 0x00, 0x02, 0x00, 0x00, 0x00, 'h'}), 0u);
    // 未対応の形式 (Guid / ByteString / 拡張)
    CHECK_EQ(dec({0x04, 0x01, 0x00}), 0u);
    CHECK_EQ(dec({0x05, 0x01, 0x00, 0x00, 0x00, 0x00}), 0u);
    CHECK_EQ(dec({0x40, 0x00, 0x01}), 0u);
    CHECK_EQ(dec({}), 0u);
    CHECK_EQ(opcua_decode_nodeid(nullptr, 2, &n), 0u);
    const uint8_t two[] = {0x00, 0x01};
    CHECK_EQ(opcua_decode_nodeid(two, 2, nullptr), 0u);

    // 往復
    const opcua_nodeid_t ids[] = {num_id(0, 0), num_id(0, 255), num_id(0, 256), num_id(255, 65535),
                                  num_id(256, 0), num_id(0, 70000), str_id(7, "Machine/Temp")};
    for (const opcua_nodeid_t &id : ids) {
        const Bytes e = enc_nodeid(id);
        CHECK_EQ(opcua_decode_nodeid(e.data(), e.size(), &n), e.size());
        CHECK_EQ(n.namespace_index, id.namespace_index);
        CHECK_EQ(n.id_type, id.id_type);
        if (id.id_type == OPCUA_NODEID_NUMERIC)
            CHECK_EQ(n.id.numeric, id.id.numeric);
        else
            CHECK(std::string(n.id.string) == id.id.string);
    }
}

// ---- DataValue (符号化マスク) ----
TEST_CASE(datavalue_encoding_mask_bytes) {
    opcua_datavalue_t dv;
    std::memset(&dv, 0, sizeof(dv));
    dv.value = int32_var(5);
    dv.status = OPCUA_STATUS_GOOD;
    uint8_t b[64];
    size_t n = opcua_encode_datavalue(&dv, b, sizeof(b));
    CHECK_BYTES(Bytes(b, b + n), 0x01, 0x06, 0x05, 0x00, 0x00, 0x00); // 値のみ
    dv.status = OPCUA_STATUS_BAD_NODE_ID_UNKNOWN;
    n = opcua_encode_datavalue(&dv, b, sizeof(b));
    CHECK_BYTES(Bytes(b, b + n), 0x03, 0x06, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x34, 0x80); // + status
    dv.status = OPCUA_STATUS_GOOD;
    dv.server_timestamp = 0x0102030405060708ULL;
    n = opcua_encode_datavalue(&dv, b, sizeof(b));
    CHECK_BYTES(Bytes(b, b + n), 0x09, 0x06, 0x05, 0x00, 0x00, 0x00, 0x08, 0x07, 0x06, 0x05, 0x04, 0x03,
                0x02, 0x01); // + server timestamp
    dv.status = OPCUA_STATUS_BAD_NOT_WRITABLE;
    n = opcua_encode_datavalue(&dv, b, sizeof(b));
    CHECK_BYTES(Bytes(b, b + n), 0x0B, 0x06, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3B, 0x80, 0x08, 0x07, 0x06,
                0x05, 0x04, 0x03, 0x02, 0x01); // 値, status, server timestamp の順
    // 容量の境界
    CHECK_EQ(opcua_encode_datavalue(&dv, b, 18), 18u);
    CHECK_EQ(opcua_encode_datavalue(&dv, b, 17), 0u);
    CHECK_EQ(opcua_encode_datavalue(&dv, b, 10), 0u);
    CHECK_EQ(opcua_encode_datavalue(&dv, b, 0), 0u);
    CHECK_EQ(opcua_encode_datavalue(nullptr, b, 64), 0u);
    CHECK_EQ(opcua_encode_datavalue(&dv, nullptr, 64), 0u);
}

TEST_CASE(datavalue_decoding_mask_handling) {
    opcua_datavalue_t dv;
    auto dec = [&](Bytes b) { return opcua_decode_datavalue(b.data(), b.size(), &dv); };
    CHECK_EQ(dec({0x01, 0x06, 0x05, 0x00, 0x00, 0x00}), 6u);
    CHECK_EQ(dv.value.value.int32_val, 5);
    CHECK_EQ(dv.status, OPCUA_STATUS_GOOD);
    CHECK_EQ(dv.server_timestamp, 0u);
    CHECK_EQ(dec({0x03, 0x06, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x34, 0x80}), 10u);
    CHECK_EQ(dv.status, OPCUA_STATUS_BAD_NODE_ID_UNKNOWN);
    CHECK_EQ(dec({0x09, 0x06, 0x05, 0x00, 0x00, 0x00, 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01}), 14u);
    CHECK_EQ(dv.server_timestamp, 0x0102030405060708ULL);
    // ステータスだけ / 値なし
    CHECK_EQ(dec({0x02, 0x00, 0x00, 0x3B, 0x80}), 5u);
    CHECK_EQ(dv.status, OPCUA_STATUS_BAD_NOT_WRITABLE);
    CHECK_EQ(dv.value.type, OPCUA_TYPE_NULL);
    CHECK_EQ(dec({0x00}), 1u);
    CHECK_EQ(dv.status, OPCUA_STATUS_GOOD);
    // 未対応のフィールド (ソースのタイムスタンプ 0x04 / ピコ秒 0x10 0x20 / 予約ビット) を黙って読み飛ばさない
    CHECK_EQ(dec({0x05, 0x06, 0x05, 0x00, 0x00, 0x00, 0, 0, 0, 0, 0, 0, 0, 0}), 0u);
    CHECK_EQ(dec({0x11, 0x06, 0x05, 0x00, 0x00, 0x00, 0, 0}), 0u);
    CHECK_EQ(dec({0x21, 0x06, 0x05, 0x00, 0x00, 0x00, 0, 0}), 0u);
    CHECK_EQ(dec({0x41, 0x06, 0x05, 0x00, 0x00, 0x00}), 0u);
    CHECK_EQ(dec({0x81, 0x06, 0x05, 0x00, 0x00, 0x00}), 0u);
    // 切り詰め
    CHECK_EQ(dec({0x03, 0x06, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x34}), 0u);
    CHECK_EQ(dec({0x09, 0x06, 0x05, 0x00, 0x00, 0x00, 0x08, 0x07}), 0u);
    CHECK_EQ(dec({0x01}), 0u);
    CHECK_EQ(dec({}), 0u);
    CHECK_EQ(opcua_decode_datavalue(nullptr, 4, &dv), 0u);
    const uint8_t z[] = {0x00};
    CHECK_EQ(opcua_decode_datavalue(z, 1, nullptr), 0u);
}

// ---- TCP トランスポート ----
TEST_CASE(tcp_header_bytes) {
    opcua_tcp_header_t h;
    std::memset(&h, 0, sizeof(h));
    std::memcpy(h.msg_type, "MSG", 3);
    h.is_final = 'F';
    h.msg_size = 0x01020304;
    uint8_t b[16];
    std::memset(b, 0xEE, sizeof(b));
    CHECK_EQ(opcua_encode_tcp_header(&h, b, sizeof(b)), 8u);
    CHECK_BYTES(Bytes(b, b + 9), 'M', 'S', 'G', 'F', 0x04, 0x03, 0x02, 0x01, 0xEE);
    CHECK_EQ(opcua_encode_tcp_header(&h, b, 7), 0u);
    CHECK_EQ(opcua_encode_tcp_header(&h, b, 8), 8u);
    CHECK_EQ(opcua_encode_tcp_header(nullptr, b, 16), 0u);
    CHECK_EQ(opcua_encode_tcp_header(&h, nullptr, 16), 0u);

    opcua_tcp_header_t d;
    CHECK_EQ(opcua_decode_tcp_header(b, 8, &d), 8u);
    CHECK(std::string(d.msg_type) == "MSG");
    CHECK_EQ(d.is_final, 'F');
    CHECK_EQ(d.msg_size, 0x01020304u);
    CHECK_EQ(opcua_decode_tcp_header(b, 7, &d), 0u);
    CHECK_EQ(opcua_decode_tcp_header(nullptr, 8, &d), 0u);
    CHECK_EQ(opcua_decode_tcp_header(b, 8, nullptr), 0u);
}

TEST_CASE(hello_acknowledge_and_error_bytes) {
    opcua_hello_message_t hello;
    std::memset(&hello, 0, sizeof(hello));
    hello.protocol_version = 0;
    hello.receive_buffer_size = 65535;
    hello.send_buffer_size = 0x00010000;
    hello.max_message_size = 0x00100000;
    hello.max_chunk_count = 4;
    std::snprintf(hello.endpoint_url, sizeof(hello.endpoint_url), "opc.tcp://h:1");
    uint8_t b[128];
    size_t n = opcua_encode_hello(&hello, b, sizeof(b));
    const std::string url = "opc.tcp://h:1";
    const Bytes expect = cat({Bytes{'H', 'E', 'L', 'F'}, u32(8 + 20 + 4 + (uint32_t)url.size()), u32(0), u32(65535),
                              u32(0x00010000), u32(0x00100000), u32(4), str(url)});
    CHECK_EQ(n, 8u + 20 + 4 + url.size());
    CHECK(Bytes(b, b + n) == expect);
    // 容量の境界
    CHECK_EQ(opcua_encode_hello(&hello, b, n), n);
    CHECK_EQ(opcua_encode_hello(&hello, b, n - 1), 0u);
    CHECK_EQ(opcua_encode_hello(&hello, b, 31), 0u);
    CHECK_EQ(opcua_encode_hello(nullptr, b, 128), 0u);
    CHECK_EQ(opcua_encode_hello(&hello, nullptr, 128), 0u);
    // 空の URL
    hello.endpoint_url[0] = 0;
    CHECK_EQ(opcua_encode_hello(&hello, b, sizeof(b)), 32u);
    CHECK_EQ(b[4], 32);

    opcua_ack_message_t ack{1, 2, 3, 4, 5};
    n = opcua_encode_ack(&ack, b, sizeof(b));
    CHECK(Bytes(b, b + n) == cat({Bytes{'A', 'C', 'K', 'F'}, u32(28), u32(1), u32(2), u32(3), u32(4), u32(5)}));
    CHECK_EQ(opcua_encode_ack(&ack, b, 28), 28u);
    CHECK_EQ(opcua_encode_ack(&ack, b, 27), 0u);
    CHECK_EQ(opcua_encode_ack(nullptr, b, 128), 0u);
    CHECK_EQ(opcua_encode_ack(&ack, nullptr, 128), 0u);

    n = opcua_encode_error(OPCUA_STATUS_BAD_UNEXPECTED_ERROR, "bad", b, sizeof(b));
    CHECK(Bytes(b, b + n) == cat({Bytes{'E', 'R', 'R', 'F'}, u32(8 + 4 + 4 + 3), u32(0x80010000), str("bad")}));
    n = opcua_encode_error(0x80340000, nullptr, b, sizeof(b));
    CHECK(Bytes(b, b + n) == cat({Bytes{'E', 'R', 'R', 'F'}, u32(16), u32(0x80340000), u32(0xFFFFFFFFu)}));
    n = opcua_encode_error(1, "", b, sizeof(b));
    CHECK_EQ(n, 16u);
    CHECK_EQ(opcua_encode_error(1, "abc", b, 19), 19u);
    CHECK_EQ(opcua_encode_error(1, "abc", b, 18), 0u);
    CHECK_EQ(opcua_encode_error(1, nullptr, b, 16), 16u);
    CHECK_EQ(opcua_encode_error(1, nullptr, b, 15), 0u);
    CHECK_EQ(opcua_encode_error(1, "x", nullptr, 64), 0u);
}

TEST_CASE(hello_and_ack_decoding_rules) {
    opcua_hello_message_t hello;
    opcua_ack_message_t ack;
    const Bytes good_hello = cat({Bytes{'H', 'E', 'L', 'F'}, u32(8 + 20 + 4 + 2), u32(0), u32(100), u32(200), u32(300),
                                  u32(400), str("ab")});
    CHECK_EQ(opcua_decode_hello(good_hello.data(), good_hello.size(), &hello), good_hello.size());
    CHECK_EQ(hello.protocol_version, 0u);
    CHECK_EQ(hello.receive_buffer_size, 100u);
    CHECK_EQ(hello.send_buffer_size, 200u);
    CHECK_EQ(hello.max_message_size, 300u);
    CHECK_EQ(hello.max_chunk_count, 400u);
    CHECK(std::string(hello.endpoint_url) == "ab");

    auto bad_hello = [&](Bytes b) { return opcua_decode_hello(b.data(), b.size(), &hello); };
    Bytes m = good_hello;
    m[0] = 'A';
    CHECK_EQ(bad_hello(m), 0u); // 種別違い
    m = good_hello;
    m[3] = 'C';
    CHECK_EQ(bad_hello(m), 0u); // 中間チャンクは不可
    m = good_hello;
    m[4] = 31;
    CHECK_EQ(bad_hello(m), 0u); // 宣言長が最小未満
    m = good_hello;
    m[4] = (uint8_t)(good_hello.size() + 1);
    CHECK_EQ(bad_hello(m), 0u); // 宣言長が実長より長い
    m = good_hello;
    m[4] = (uint8_t)(good_hello.size() - 1);
    CHECK_EQ(bad_hello(m), 0u); // 宣言長が内容より短い
    m = good_hello;
    m.push_back(0x99);          // 宣言長より後ろのバイトは無視する
    CHECK_EQ(bad_hello(m), good_hello.size());
    CHECK_EQ(opcua_decode_hello(good_hello.data(), 31, &hello), 0u);
    CHECK_EQ(opcua_decode_hello(nullptr, 40, &hello), 0u);
    CHECK_EQ(opcua_decode_hello(good_hello.data(), good_hello.size(), nullptr), 0u);
    // 空 / null の URL
    const Bytes nullurl = cat({Bytes{'H', 'E', 'L', 'F'}, u32(32), u32(0), u32(1), u32(2), u32(3), u32(4), u32(0xFFFFFFFFu)});
    CHECK_EQ(opcua_decode_hello(nullurl.data(), nullurl.size(), &hello), 32u);
    CHECK_EQ(hello.endpoint_url[0], 0);
    // URL が長すぎて収まらない (63 文字まで)
    const std::string u63(63, 'u'), u64s(64, 'u');
    Bytes h63 = cat({Bytes{'H', 'E', 'L', 'F'}, u32(8 + 20 + 4 + 63), Bytes(20, 0), str(u63)});
    CHECK_EQ(opcua_decode_hello(h63.data(), h63.size(), &hello), h63.size());
    Bytes h64 = cat({Bytes{'H', 'E', 'L', 'F'}, u32(8 + 20 + 4 + 64), Bytes(20, 0), str(u64s)});
    CHECK_EQ(opcua_decode_hello(h64.data(), h64.size(), &hello), 0u);

    const Bytes good_ack = cat({Bytes{'A', 'C', 'K', 'F'}, u32(28), u32(1), u32(2), u32(3), u32(4), u32(5)});
    CHECK_EQ(opcua_decode_ack(good_ack.data(), good_ack.size(), &ack), 28u);
    CHECK_EQ(ack.protocol_version, 1u);
    CHECK_EQ(ack.receive_buffer_size, 2u);
    CHECK_EQ(ack.send_buffer_size, 3u);
    CHECK_EQ(ack.max_message_size, 4u);
    CHECK_EQ(ack.max_chunk_count, 5u);
    auto bad_ack = [&](Bytes b) { return opcua_decode_ack(b.data(), b.size(), &ack); };
    m = good_ack;
    m[0] = 'H';
    CHECK_EQ(bad_ack(m), 0u);
    m = good_ack;
    m[3] = 'C';
    CHECK_EQ(bad_ack(m), 0u);
    m = good_ack;
    m[4] = 27;
    CHECK_EQ(bad_ack(m), 0u);
    m = good_ack;
    m[4] = 29;
    CHECK_EQ(bad_ack(m), 0u);
    CHECK_EQ(opcua_decode_ack(good_ack.data(), 27, &ack), 0u);
    CHECK_EQ(opcua_decode_ack(nullptr, 28, &ack), 0u);
    CHECK_EQ(opcua_decode_ack(good_ack.data(), 28, nullptr), 0u);
}

// ---- アドレス空間と読み書きサービス ----
namespace {
struct Srv {
    opcua_server_t s;
    Srv() {
        opcua_server_init(&s, "opc.tcp://dev:4840");
        const opcua_nodeid_t rw = num_id(1, 1000), ro = num_id(1, 1001), wo = num_id(1, 1002), none = num_id(1, 1003);
        opcua_variant_t i5 = int32_var(5);
        REQUIRE(opcua_server_add_variable(&s, &rw, "Temp", &i5, 0x03));
        REQUIRE(opcua_server_add_variable(&s, &ro, "Gauge", &i5, 0x01));
        REQUIRE(opcua_server_add_variable(&s, &wo, "Setpoint", &i5, 0x02));
        REQUIRE(opcua_server_add_variable(&s, &none, "Hidden", &i5, 0x00));
    }
};
opcua_datavalue_t dv_of(const opcua_variant_t &v) {
    opcua_datavalue_t d;
    std::memset(&d, 0, sizeof(d));
    d.value = v;
    return d;
}
} // namespace

TEST_CASE(server_init_and_address_space) {
    opcua_server_t s;
    opcua_server_init(&s, nullptr);
    CHECK(std::string(s.endpoint_url) == "opc.tcp://localhost:4840");
    CHECK_EQ(s.node_count, 0u);
    opcua_server_init(&s, "opc.tcp://abc:1");
    CHECK(std::string(s.endpoint_url) == "opc.tcp://abc:1");
    const std::string longurl(200, 'u');
    opcua_server_init(&s, longurl.c_str());
    CHECK_EQ(std::strlen(s.endpoint_url), (size_t)(OPCUA_MAX_STRING_LEN - 1));
    opcua_server_init(nullptr, "x");

    opcua_nodeid_t id = num_id(2, 5);
    opcua_variant_t v = int32_var(1);
    CHECK(opcua_server_add_variable(&s, &id, "A", &v, 3));
    CHECK(!opcua_server_add_variable(&s, &id, "B", &v, 3));          // 同じ NodeId は不可
    opcua_nodeid_t other_ns = num_id(3, 5);
    CHECK(opcua_server_add_variable(&s, &other_ns, "C", &v, 3));      // 名前空間が違えば別
    opcua_nodeid_t sid = str_id(2, "5"), sid2 = str_id(2, "6");
    CHECK(opcua_server_add_variable(&s, &sid, "D", &v, 3));           // 文字列と数値は別
    CHECK(!opcua_server_add_variable(&s, &sid, "E", &v, 3));
    CHECK(opcua_server_add_variable(&s, &sid2, nullptr, nullptr, 3)); // 名前・初期値なし
    CHECK_EQ(s.node_count, 4u);
    CHECK(!opcua_server_add_variable(nullptr, &id, "A", &v, 3));
    CHECK(!opcua_server_add_variable(&s, nullptr, "A", &v, 3));

    // 上限
    opcua_server_t full;
    opcua_server_init(&full, nullptr);
    for (uint32_t i = 0; i < OPCUA_MAX_NODES; ++i) {
        opcua_nodeid_t n = num_id(1, i);
        CHECK(opcua_server_add_variable(&full, &n, "n", &v, 3));
    }
    opcua_nodeid_t extra = num_id(1, 999);
    CHECK(!opcua_server_add_variable(&full, &extra, "n", &v, 3));
    CHECK_EQ(full.node_count, (size_t)OPCUA_MAX_NODES);
    opcua_nodeid_t last = num_id(1, OPCUA_MAX_NODES - 1);
    opcua_datavalue_t out;
    CHECK(opcua_server_read_value(&full, &last, &out)); // 最後のノードにも届く
}

TEST_CASE(server_value_api) {
    Srv f;
    const opcua_nodeid_t rw = num_id(1, 1000), missing = num_id(1, 4242);
    opcua_datavalue_t out;
    CHECK(opcua_server_read_value(&f.s, &rw, &out));
    CHECK_EQ(out.value.value.int32_val, 5);
    CHECK_EQ(out.status, OPCUA_STATUS_GOOD);
    opcua_variant_t nv = int32_var(77);
    CHECK(opcua_server_write_value(&f.s, &rw, &nv)); // アプリケーションからの書き込みはアクセスレベルに依らない
    const opcua_nodeid_t ro = num_id(1, 1001);
    CHECK(opcua_server_write_value(&f.s, &ro, &nv));
    CHECK(opcua_server_read_value(&f.s, &ro, &out));
    CHECK_EQ(out.value.value.int32_val, 77);
    CHECK(!opcua_server_read_value(&f.s, &missing, &out));
    CHECK(!opcua_server_write_value(&f.s, &missing, &nv));
    CHECK(!opcua_server_read_value(nullptr, &rw, &out));
    CHECK(!opcua_server_read_value(&f.s, nullptr, &out));
    CHECK(!opcua_server_read_value(&f.s, &rw, nullptr));
    CHECK(!opcua_server_write_value(nullptr, &rw, &nv));
    CHECK(!opcua_server_write_value(&f.s, nullptr, &nv));
    CHECK(!opcua_server_write_value(&f.s, &rw, nullptr));
    // 文字列 NodeId の探索
    opcua_nodeid_t sid = str_id(1, "Pump/Speed"), sid_other = str_id(1, "Pump/Speed2");
    opcua_variant_t d = var(OPCUA_TYPE_DOUBLE);
    d.value.double_val = 12.5;
    CHECK(opcua_server_add_variable(&f.s, &sid, "Speed", &d, 3));
    CHECK(opcua_server_read_value(&f.s, &sid, &out));
    CHECK_EQ(out.value.value.double_val, 12.5);
    CHECK(!opcua_server_read_value(&f.s, &sid_other, &out));
}

TEST_CASE(read_service_status_codes) {
    Srv f;
    opcua_datavalue_t out;
    std::memset(&out, 0, sizeof(out));
    // 存在しないノード
    const opcua_nodeid_t missing = num_id(1, 4242), wrong_ns = num_id(2, 1000);
    CHECK_EQ(opcua_server_process_read(&f.s, &missing, OPCUA_ATTR_VALUE, &out), OPCUA_STATUS_BAD_NODE_ID_UNKNOWN);
    CHECK_EQ(out.status, OPCUA_STATUS_BAD_NODE_ID_UNKNOWN);
    CHECK_EQ(opcua_server_process_read(&f.s, &wrong_ns, OPCUA_ATTR_VALUE, &out), OPCUA_STATUS_BAD_NODE_ID_UNKNOWN);
    // Value
    const opcua_nodeid_t rw = num_id(1, 1000), ro = num_id(1, 1001), wo = num_id(1, 1002), none = num_id(1, 1003);
    CHECK_EQ(opcua_server_process_read(&f.s, &rw, OPCUA_ATTR_VALUE, &out), OPCUA_STATUS_GOOD);
    CHECK_EQ(out.value.type, OPCUA_TYPE_INT32);
    CHECK_EQ(out.value.value.int32_val, 5);
    CHECK_EQ(opcua_server_process_read(&f.s, &ro, OPCUA_ATTR_VALUE, &out), OPCUA_STATUS_GOOD);
    // 読み取り権限がない Value
    CHECK_EQ(opcua_server_process_read(&f.s, &wo, OPCUA_ATTR_VALUE, &out), OPCUA_STATUS_BAD_NOT_READABLE);
    CHECK_EQ(out.status, OPCUA_STATUS_BAD_NOT_READABLE);
    CHECK_EQ(opcua_server_process_read(&f.s, &none, OPCUA_ATTR_VALUE, &out), OPCUA_STATUS_BAD_NOT_READABLE);
    // アクセスレベルは Value にだけ効く: 他の属性は書き込み専用ノードでも読める
    CHECK_EQ(opcua_server_process_read(&f.s, &wo, OPCUA_ATTR_DISPLAY_NAME, &out), OPCUA_STATUS_GOOD);
    CHECK(std::string(out.value.value.string_val) == "Setpoint");
    CHECK_EQ(opcua_server_process_read(&f.s, &none, OPCUA_ATTR_NODE_CLASS, &out), OPCUA_STATUS_GOOD);
    // 属性ごとの応答
    CHECK_EQ(opcua_server_process_read(&f.s, &rw, OPCUA_ATTR_NODE_ID, &out), OPCUA_STATUS_GOOD);
    CHECK_EQ(out.value.type, OPCUA_TYPE_UINT32);
    CHECK_EQ(out.value.value.uint32_val, 1000u);
    CHECK_EQ(out.status, OPCUA_STATUS_GOOD);
    CHECK_EQ(opcua_server_process_read(&f.s, &rw, OPCUA_ATTR_DISPLAY_NAME, &out), OPCUA_STATUS_GOOD);
    CHECK_EQ(out.value.type, OPCUA_TYPE_STRING);
    CHECK(std::string(out.value.value.string_val) == "Temp");
    CHECK_EQ(opcua_server_process_read(&f.s, &rw, OPCUA_ATTR_NODE_CLASS, &out), OPCUA_STATUS_GOOD);
    CHECK_EQ(out.value.type, OPCUA_TYPE_INT32);
    CHECK_EQ(out.value.value.int32_val, (int)OPCUA_NODECLASS_VARIABLE);
    CHECK_EQ(opcua_server_process_read(&f.s, &ro, OPCUA_ATTR_ACCESS_LEVEL, &out), OPCUA_STATUS_GOOD);
    CHECK_EQ(out.value.type, OPCUA_TYPE_BYTE);
    CHECK_EQ(out.value.value.byte_val, 0x01);
    CHECK_EQ(opcua_server_process_read(&f.s, &rw, OPCUA_ATTR_VALUE_RANK, &out), OPCUA_STATUS_GOOD);
    CHECK_EQ(out.value.type, OPCUA_TYPE_INT32);
    CHECK_EQ(out.value.value.int32_val, -1); // スカラ
    // 未知の属性 ID
    CHECK_EQ(opcua_server_process_read(&f.s, &rw, (opcua_attribute_id_t)99, &out), OPCUA_STATUS_BAD_ATTRIBUTE_ID_INVALID);
    CHECK_EQ(out.status, OPCUA_STATUS_BAD_ATTRIBUTE_ID_INVALID);
    CHECK_EQ(opcua_server_process_read(&f.s, &rw, (opcua_attribute_id_t)0, &out), OPCUA_STATUS_BAD_ATTRIBUTE_ID_INVALID);
    // 引数不正
    CHECK_EQ(opcua_server_process_read(nullptr, &rw, OPCUA_ATTR_VALUE, &out), OPCUA_STATUS_BAD_UNEXPECTED_ERROR);
    CHECK_EQ(opcua_server_process_read(&f.s, nullptr, OPCUA_ATTR_VALUE, &out), OPCUA_STATUS_BAD_UNEXPECTED_ERROR);
    CHECK_EQ(opcua_server_process_read(&f.s, &rw, OPCUA_ATTR_VALUE, nullptr), OPCUA_STATUS_BAD_UNEXPECTED_ERROR);
}

TEST_CASE(status_code_constants) {
    CHECK_EQ(OPCUA_STATUS_GOOD, 0u);
    CHECK_EQ(OPCUA_STATUS_BAD_UNEXPECTED_ERROR, 0x80010000u);
    CHECK_EQ(OPCUA_STATUS_BAD_NODE_ID_UNKNOWN, 0x80340000u);
    CHECK_EQ(OPCUA_STATUS_BAD_ATTRIBUTE_ID_INVALID, 0x80350000u);
    CHECK_EQ(OPCUA_STATUS_BAD_NOT_READABLE, 0x803A0000u);
    CHECK_EQ(OPCUA_STATUS_BAD_NOT_WRITABLE, 0x803B0000u);
    CHECK_EQ(OPCUA_STATUS_BAD_TYPE_MISMATCH, 0x80740000u);
    CHECK_EQ((int)OPCUA_ATTR_NODE_ID, 1);
    CHECK_EQ((int)OPCUA_ATTR_NODE_CLASS, 2);
    CHECK_EQ((int)OPCUA_ATTR_BROWSE_NAME, 3);
    CHECK_EQ((int)OPCUA_ATTR_DISPLAY_NAME, 4);
    CHECK_EQ((int)OPCUA_ATTR_VALUE, 13);
    CHECK_EQ((int)OPCUA_ATTR_DATA_TYPE, 14);
    CHECK_EQ((int)OPCUA_ATTR_VALUE_RANK, 15);
    CHECK_EQ((int)OPCUA_ATTR_ACCESS_LEVEL, 17);
    CHECK_EQ((int)OPCUA_NODECLASS_VARIABLE, 2);
}

TEST_CASE(write_service_status_codes) {
    Srv f;
    const opcua_nodeid_t rw = num_id(1, 1000), ro = num_id(1, 1001), wo = num_id(1, 1002), none = num_id(1, 1003),
                         missing = num_id(1, 4242);
    const opcua_datavalue_t i9 = dv_of(int32_var(9));
    CHECK_EQ(opcua_server_process_write(&f.s, &missing, OPCUA_ATTR_VALUE, &i9), OPCUA_STATUS_BAD_NODE_ID_UNKNOWN);
    CHECK_EQ(opcua_server_process_write(&f.s, &ro, OPCUA_ATTR_VALUE, &i9), OPCUA_STATUS_BAD_NOT_WRITABLE);
    CHECK_EQ(opcua_server_process_write(&f.s, &none, OPCUA_ATTR_VALUE, &i9), OPCUA_STATUS_BAD_NOT_WRITABLE);
    opcua_datavalue_t out;
    CHECK(opcua_server_read_value(&f.s, &ro, &out));
    CHECK_EQ(out.value.value.int32_val, 5); // 拒否した書き込みは反映されない

    CHECK_EQ(opcua_server_process_write(&f.s, &rw, OPCUA_ATTR_VALUE, &i9), OPCUA_STATUS_GOOD);
    CHECK(opcua_server_read_value(&f.s, &rw, &out));
    CHECK_EQ(out.value.value.int32_val, 9);
    CHECK_EQ(opcua_server_process_write(&f.s, &wo, OPCUA_ATTR_VALUE, &i9), OPCUA_STATUS_GOOD);
    // 読み取り権限のないノードに書いた値は Value 属性としては読めない
    CHECK_EQ(opcua_server_process_read(&f.s, &wo, OPCUA_ATTR_VALUE, &out), OPCUA_STATUS_BAD_NOT_READABLE);

    // 型の不一致: ノードは Int32 なので Double / String / Boolean は書けない
    opcua_variant_t d = var(OPCUA_TYPE_DOUBLE), s = var(OPCUA_TYPE_STRING), b = var(OPCUA_TYPE_BOOLEAN),
                    u = var(OPCUA_TYPE_UINT32);
    for (const opcua_variant_t &bad : {d, s, b, u}) {
        const opcua_datavalue_t dv = dv_of(bad);
        CHECK_EQ(opcua_server_process_write(&f.s, &rw, OPCUA_ATTR_VALUE, &dv), OPCUA_STATUS_BAD_TYPE_MISMATCH);
    }
    CHECK(opcua_server_read_value(&f.s, &rw, &out));
    CHECK_EQ(out.value.type, OPCUA_TYPE_INT32);
    CHECK_EQ(out.value.value.int32_val, 9); // 型違いの書き込みは反映されない
    // 型が定まっていないノード (初期値なし) は最初の書き込みで型が決まる
    opcua_nodeid_t free_node = num_id(1, 2000);
    CHECK(opcua_server_add_variable(&f.s, &free_node, "Free", nullptr, 0x03));
    const opcua_datavalue_t dd = dv_of(d);
    CHECK_EQ(opcua_server_process_write(&f.s, &free_node, OPCUA_ATTR_VALUE, &dd), OPCUA_STATUS_GOOD);
    CHECK_EQ(opcua_server_process_write(&f.s, &free_node, OPCUA_ATTR_VALUE, &i9), OPCUA_STATUS_BAD_TYPE_MISMATCH);

    // Value 以外の属性: 既知の属性は読み取り専用 (BadNotWritable)、未知の属性 ID は BadAttributeIdInvalid
    for (opcua_attribute_id_t a : {OPCUA_ATTR_NODE_ID, OPCUA_ATTR_NODE_CLASS, OPCUA_ATTR_BROWSE_NAME,
                                   OPCUA_ATTR_DISPLAY_NAME, OPCUA_ATTR_DATA_TYPE, OPCUA_ATTR_VALUE_RANK,
                                   OPCUA_ATTR_ACCESS_LEVEL}) {
        CHECK_EQ(opcua_server_process_write(&f.s, &rw, a, &i9), OPCUA_STATUS_BAD_NOT_WRITABLE);
    }
    CHECK_EQ(opcua_server_process_write(&f.s, &rw, (opcua_attribute_id_t)99, &i9), OPCUA_STATUS_BAD_ATTRIBUTE_ID_INVALID);
    CHECK_EQ(opcua_server_process_write(&f.s, &rw, (opcua_attribute_id_t)0, &i9), OPCUA_STATUS_BAD_ATTRIBUTE_ID_INVALID);

    CHECK_EQ(opcua_server_process_write(nullptr, &rw, OPCUA_ATTR_VALUE, &i9), OPCUA_STATUS_BAD_UNEXPECTED_ERROR);
    CHECK_EQ(opcua_server_process_write(&f.s, nullptr, OPCUA_ATTR_VALUE, &i9), OPCUA_STATUS_BAD_UNEXPECTED_ERROR);
    CHECK_EQ(opcua_server_process_write(&f.s, &rw, OPCUA_ATTR_VALUE, nullptr), OPCUA_STATUS_BAD_UNEXPECTED_ERROR);
}

TEST_CASE(tcp_hello_gets_an_acknowledge) {
    Srv f;
    opcua_hello_message_t hello;
    std::memset(&hello, 0, sizeof(hello));
    hello.protocol_version = 0;
    hello.receive_buffer_size = 8192;
    hello.send_buffer_size = 8192;
    hello.max_message_size = 0;
    hello.max_chunk_count = 0;
    std::snprintf(hello.endpoint_url, sizeof(hello.endpoint_url), "opc.tcp://dev:4840");
    uint8_t req[128], resp[128];
    const size_t rn = opcua_encode_hello(&hello, req, sizeof(req));
    const size_t n = opcua_server_process_tcp_message(&f.s, req, rn, resp, sizeof(resp));
    CHECK(Bytes(resp, resp + n) == cat({Bytes{'A', 'C', 'K', 'F'}, u32(28), u32(0), u32(65535), u32(65535),
                                        u32(OPCUA_MAX_MESSAGE_SIZE), u32(1)}));
    // プロトコルバージョンは Hello の値を返す
    hello.protocol_version = 3;
    const size_t rn3 = opcua_encode_hello(&hello, req, sizeof(req));
    opcua_server_process_tcp_message(&f.s, req, rn3, resp, sizeof(resp));
    opcua_ack_message_t ack;
    CHECK_EQ(opcua_decode_ack(resp, 28, &ack), 28u);
    CHECK_EQ(ack.protocol_version, 3u);
    // 応答バッファが足りない
    CHECK_EQ(opcua_server_process_tcp_message(&f.s, req, rn, resp, 27), 0u);
    CHECK_EQ(opcua_server_process_tcp_message(&f.s, req, rn, resp, 28), 28u);
    // 宣言長が受信長と合わない / 中間チャンク / 壊れた Hello
    Bytes m(req, req + rn);
    m[4] = (uint8_t)(rn + 1);
    CHECK_EQ(opcua_server_process_tcp_message(&f.s, m.data(), m.size(), resp, sizeof(resp)), 0u);
    m.assign(req, req + rn);
    m[3] = 'C';
    CHECK_EQ(opcua_server_process_tcp_message(&f.s, m.data(), m.size(), resp, sizeof(resp)), 0u);
    m.assign(req, req + rn);
    m[0] = 'X';
    CHECK_EQ(opcua_server_process_tcp_message(&f.s, m.data(), m.size(), resp, sizeof(resp)), 0u);
    // 引数不正
    CHECK_EQ(opcua_server_process_tcp_message(nullptr, req, rn, resp, sizeof(resp)), 0u);
    CHECK_EQ(opcua_server_process_tcp_message(&f.s, nullptr, rn, resp, sizeof(resp)), 0u);
    CHECK_EQ(opcua_server_process_tcp_message(&f.s, req, 7, resp, sizeof(resp)), 0u);
    CHECK_EQ(opcua_server_process_tcp_message(&f.s, req, rn, nullptr, sizeof(resp)), 0u);
    CHECK_EQ(opcua_server_process_tcp_message(&f.s, req, rn, resp, 7), 0u);
}

TEST_CASE(tcp_msg_returns_the_first_node_value) {
    // 簡易実装: 要求の内容に依らず、先頭ノードの DataValue を MSG で返す
    Srv f;
    const Bytes req = cat({Bytes{'M', 'S', 'G', 'F'}, u32(8)});
    uint8_t resp[64];
    size_t n = opcua_server_process_tcp_message(&f.s, req.data(), req.size(), resp, sizeof(resp));
    CHECK(Bytes(resp, resp + n) == cat({Bytes{'M', 'S', 'G', 'F'}, u32(8 + 6), Bytes{0x01, 0x06, 0x05, 0x00, 0x00, 0x00}}));
    CHECK_EQ(opcua_server_process_tcp_message(&f.s, req.data(), req.size(), resp, 13), 0u); // 値が入らない
    CHECK_EQ(opcua_server_process_tcp_message(&f.s, req.data(), req.size(), resp, 14), 14u);
    // ノードがなければヘッダだけ
    opcua_server_t empty;
    opcua_server_init(&empty, nullptr);
    n = opcua_server_process_tcp_message(&empty, req.data(), req.size(), resp, sizeof(resp));
    CHECK(Bytes(resp, resp + n) == cat({Bytes{'M', 'S', 'G', 'F'}, u32(8)}));
    // 未対応のメッセージ種別
    const Bytes opn = cat({Bytes{'O', 'P', 'N', 'F'}, u32(8)});
    CHECK_EQ(opcua_server_process_tcp_message(&f.s, opn.data(), opn.size(), resp, sizeof(resp)), 0u);
    const Bytes chunk = cat({Bytes{'M', 'S', 'G', 'C'}, u32(8)});
    CHECK_EQ(opcua_server_process_tcp_message(&f.s, chunk.data(), chunk.size(), resp, sizeof(resp)), 0u);
}

// ---- 境界・引数の組み合わせ ----
TEST_CASE(zero_length_and_exact_capacity_boundaries) {
    uint8_t buf[64];
    std::memset(buf, 0, sizeof(buf));
    // 有効なポインタでも、長さ 0 / 足りない長さなら何も読まない
    opcua_variant_t v;
    opcua_nodeid_t n;
    opcua_datavalue_t dv;
    opcua_tcp_header_t th;
    opcua_hello_message_t hello;
    opcua_ack_message_t ack;
    char s[16];
    CHECK_EQ(opcua_decode_variant(buf, 0, &v), 0u);
    CHECK_EQ(opcua_decode_nodeid(buf, 0, &n), 0u);
    CHECK_EQ(opcua_decode_datavalue(buf, 0, &dv), 0u);
    CHECK_EQ(opcua_decode_string(buf, 0, s, sizeof(s)), 0u);
    CHECK_EQ(opcua_decode_string(buf, 3, s, sizeof(s)), 0u);
    CHECK_EQ(opcua_decode_string(buf, 4, s, sizeof(s)), 4u); // 長さ 0 の文字列
    for (size_t len = 0; len < 8; ++len) CHECK_EQ(opcua_decode_tcp_header(buf, len, &th), 0u);
    for (size_t len = 0; len < 32; ++len) CHECK_EQ(opcua_decode_hello(buf, len, &hello), 0u);
    for (size_t len = 0; len < 28; ++len) CHECK_EQ(opcua_decode_ack(buf, len, &ack), 0u);
    // 1 バイトちょうどで足りる型 (Null の Variant / DataValue) は長さ 1 で復号できる
    buf[0] = 0x00;
    CHECK_EQ(opcua_decode_variant(buf, 1, &v), 1u);
    CHECK_EQ(opcua_decode_datavalue(buf, 1, &dv), 1u);
    // 符号化側: 容量 0 / ちょうど
    opcua_variant_t nullv = var(OPCUA_TYPE_NULL);
    CHECK_EQ(opcua_encode_variant(&nullv, buf, 0), 0u);
    CHECK_EQ(opcua_encode_variant(&nullv, buf, 1), 1u);
    opcua_nodeid_t id = num_id(0, 1);
    CHECK_EQ(opcua_encode_nodeid(&id, buf, 0), 0u);
    opcua_datavalue_t d0;
    std::memset(&d0, 0, sizeof(d0));
    d0.value = nullv;
    CHECK_EQ(opcua_encode_datavalue(&d0, buf, 0), 0u);
    CHECK_EQ(opcua_encode_datavalue(&d0, buf, 1), 0u); // マスク 1 バイト + Null の Variant 1 バイト = 2 が必要
    CHECK_EQ(opcua_encode_datavalue(&d0, buf, 2), 2u);
    opcua_ack_message_t a0{};
    CHECK_EQ(opcua_encode_ack(&a0, buf, 0), 0u);
    opcua_hello_message_t h0;
    std::memset(&h0, 0, sizeof(h0));
    CHECK_EQ(opcua_encode_hello(&h0, buf, 0), 0u);
    CHECK_EQ(opcua_encode_hello(&h0, buf, 32), 32u); // 空の URL ならちょうど 32 バイト
    CHECK_EQ(opcua_encode_error(1, nullptr, buf, 0), 0u);
    CHECK_EQ(opcua_encode_string("", buf, 0), 0u);
    CHECK_EQ(opcua_encode_tcp_header(nullptr, buf, 8), 0u);
}

TEST_CASE(nodeid_format_boundaries_need_exactly_their_size) {
    uint8_t b[16];
    // 形式が切り替わる境界の値は、その形式の大きさの容量で符号化できる
    opcua_nodeid_t t255 = num_id(0, 255), f256 = num_id(0, 256), f65535 = num_id(0, 65535), n255 = num_id(255, 1),
                   n256 = num_id(256, 1), big = num_id(0, 65536);
    CHECK_EQ(opcua_encode_nodeid(&t255, b, 2), 2u);    // two-byte
    CHECK_EQ(opcua_encode_nodeid(&f256, b, 4), 4u);    // four-byte
    CHECK_EQ(opcua_encode_nodeid(&f256, b, 3), 0u);
    CHECK_EQ(opcua_encode_nodeid(&f65535, b, 4), 4u);  // four-byte の最大値
    CHECK_EQ(opcua_encode_nodeid(&n255, b, 4), 4u);    // 名前空間 255 も four-byte
    CHECK_EQ(opcua_encode_nodeid(&n256, b, 7), 7u);    // 名前空間 256 は numeric
    CHECK_EQ(opcua_encode_nodeid(&n256, b, 6), 0u);
    CHECK_EQ(opcua_encode_nodeid(&big, b, 7), 7u);     // 65536 は numeric
    CHECK_EQ(opcua_encode_nodeid(&big, b, 6), 0u);
    opcua_nodeid_t s = str_id(1, "ab");
    CHECK_EQ(opcua_encode_nodeid(&s, b, 9), 9u);
    CHECK_EQ(opcua_encode_nodeid(&s, b, 7), 0u);
    CHECK_EQ(opcua_encode_nodeid(&s, b, 8), 0u);
}

TEST_CASE(long_names_are_truncated_and_terminated) {
    const std::string longname(200, 'n');
    opcua_server_t s;
    opcua_server_init(&s, nullptr);
    opcua_nodeid_t id = num_id(1, 1);
    opcua_variant_t v = int32_var(1);
    CHECK(opcua_server_add_variable(&s, &id, longname.c_str(), &v, 3));
    CHECK_EQ(std::strlen(s.nodes[0].browse_name.name), (size_t)(OPCUA_MAX_STRING_LEN - 1));
    CHECK_EQ(std::strlen(s.nodes[0].display_name.text), (size_t)(OPCUA_MAX_STRING_LEN - 1));
    CHECK_EQ(s.nodes[0].browse_name.namespace_index, 1u);
    opcua_datavalue_t out;
    CHECK_EQ(opcua_server_process_read(&s, &id, OPCUA_ATTR_DISPLAY_NAME, &out), OPCUA_STATUS_GOOD);
    CHECK_EQ(std::strlen(out.value.value.string_val), (size_t)(OPCUA_MAX_STRING_LEN - 1));
}

TEST_CASE(server_edge_cases) {
    // 満杯のアドレス空間で、存在しない NodeId を探しても範囲外を読まない
    opcua_server_t full;
    opcua_server_init(&full, nullptr);
    opcua_variant_t v = int32_var(1);
    for (uint32_t i = 0; i < OPCUA_MAX_NODES; ++i) {
        opcua_nodeid_t n = num_id(1, i);
        opcua_server_add_variable(&full, &n, "n", &v, 3);
    }
    opcua_nodeid_t missing = num_id(9, 9);
    opcua_datavalue_t out;
    CHECK(!opcua_server_read_value(&full, &missing, &out));
    CHECK_EQ(opcua_server_process_read(&full, &missing, OPCUA_ATTR_VALUE, &out), OPCUA_STATUS_BAD_NODE_ID_UNKNOWN);

    // MSG: 応答バッファがちょうど 8 バイトでも、ノードがなければヘッダだけを返せる
    opcua_server_t empty;
    opcua_server_init(&empty, nullptr);
    const Bytes req = cat({Bytes{'M', 'S', 'G', 'F'}, u32(8)});
    uint8_t resp[16];
    CHECK_EQ(opcua_server_process_tcp_message(&empty, req.data(), req.size(), resp, 8), 8u);
    CHECK_EQ(opcua_server_process_tcp_message(&empty, req.data(), req.size(), resp, 7), 0u);
    // 受信長が 8 バイトちょうどのメッセージも扱える (最小のヘッダ)
    CHECK_EQ(opcua_server_process_tcp_message(&empty, req.data(), 8, resp, sizeof(resp)), 8u);
    // 宣言長が実際より短い
    Bytes shorter = req;
    shorter.push_back(0);
    CHECK_EQ(opcua_server_process_tcp_message(&empty, shorter.data(), shorter.size(), resp, sizeof(resp)), 0u);
    // 文字列 NodeId の NodeId 属性
    opcua_server_t s;
    opcua_server_init(&s, nullptr);
    opcua_nodeid_t sid = str_id(1, "Pump/Speed");
    opcua_server_add_variable(&s, &sid, "Speed", &v, 3);
    CHECK_EQ(opcua_server_process_read(&s, &sid, OPCUA_ATTR_NODE_ID, &out), OPCUA_STATUS_GOOD);
    CHECK_EQ(out.value.type, OPCUA_TYPE_STRING);
    CHECK(std::string(out.value.value.string_val) == "Pump/Speed");
}

TEST_CASE(null_pointers_and_short_lengths_are_rejected_by_every_codec) {
    uint8_t buf[64];
    std::memset(buf, 0, sizeof(buf));
    // 出力先 (dst) が NULL でも、容量が十分でも書き込まない
    CHECK_EQ(opcua_encode_u8(1, nullptr, 64), 0u);
    CHECK_EQ(opcua_encode_u16(1, nullptr, 64), 0u);
    CHECK_EQ(opcua_encode_u32(1, nullptr, 64), 0u);
    CHECK_EQ(opcua_encode_u64(1, nullptr, 64), 0u);
    CHECK_EQ(opcua_encode_float(1, nullptr, 64), 0u);
    CHECK_EQ(opcua_encode_double(1, nullptr, 64), 0u);
    CHECK_EQ(opcua_encode_string("a", nullptr, 64), 0u);
    CHECK_EQ(opcua_encode_string(nullptr, nullptr, 64), 0u);
    opcua_variant_t v = int32_var(1);
    opcua_datavalue_t dv = {};
    dv.value = v;
    opcua_nodeid_t id = num_id(0, 1);
    opcua_tcp_header_t th = {};
    opcua_hello_message_t hello = {};
    opcua_ack_message_t ack = {};
    CHECK_EQ(opcua_encode_variant(&v, nullptr, 64), 0u);
    CHECK_EQ(opcua_encode_datavalue(&dv, nullptr, 64), 0u);
    CHECK_EQ(opcua_encode_nodeid(&id, nullptr, 64), 0u);
    CHECK_EQ(opcua_encode_tcp_header(&th, nullptr, 64), 0u);
    CHECK_EQ(opcua_encode_hello(&hello, nullptr, 64), 0u);
    CHECK_EQ(opcua_encode_ack(&ack, nullptr, 64), 0u);
    CHECK_EQ(opcua_encode_error(1, "x", nullptr, 64), 0u);
    // 入力が NULL / 出力先が NULL でも読み書きしない
    uint8_t v8;
    uint16_t v16;
    uint32_t v32;
    uint64_t v64;
    float f;
    double d;
    char s[8];
    CHECK_EQ(opcua_decode_u8(nullptr, 64, &v8), 0u);
    CHECK_EQ(opcua_decode_u16(nullptr, 64, &v16), 0u);
    CHECK_EQ(opcua_decode_u16(buf, 64, nullptr), 0u);
    CHECK_EQ(opcua_decode_u32(nullptr, 64, &v32), 0u);
    CHECK_EQ(opcua_decode_u64(nullptr, 64, &v64), 0u);
    CHECK_EQ(opcua_decode_u64(buf, 64, nullptr), 0u);
    CHECK_EQ(opcua_decode_float(nullptr, 64, &f), 0u);
    CHECK_EQ(opcua_decode_double(nullptr, 64, &d), 0u);
    CHECK_EQ(opcua_decode_string(nullptr, 64, s, sizeof(s)), 0u);
    CHECK_EQ(opcua_decode_string(buf, 64, nullptr, sizeof(s)), 0u);
    CHECK_EQ(opcua_decode_variant(nullptr, 64, &v), 0u);
    CHECK_EQ(opcua_decode_nodeid(nullptr, 64, &id), 0u);
    CHECK_EQ(opcua_decode_datavalue(nullptr, 64, &dv), 0u);
    CHECK_EQ(opcua_decode_tcp_header(nullptr, 64, &th), 0u);
    CHECK_EQ(opcua_decode_tcp_header(buf, 64, nullptr), 0u);
    CHECK_EQ(opcua_decode_hello(nullptr, 64, &hello), 0u);
    CHECK_EQ(opcua_decode_ack(nullptr, 64, &ack), 0u);
    // 長さが 1 バイト足りない (有効なポインタ)
    const uint8_t src[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    CHECK_EQ(opcua_decode_u16(src, 1, &v16), 0u);
    CHECK_EQ(opcua_decode_u32(src, 3, &v32), 0u);
    CHECK_EQ(opcua_decode_u64(src, 7, &v64), 0u);
    CHECK_EQ(opcua_decode_float(src, 3, &f), 0u);
    CHECK_EQ(opcua_decode_double(src, 7, &d), 0u);
    CHECK_EQ(opcua_encode_tcp_header(&th, buf, 7), 0u);
    CHECK_EQ(opcua_encode_error(1, "x", buf, 16), 0u); // 16 + 1 バイト必要
    CHECK_EQ(opcua_encode_error(1, "x", buf, 17), 17u);
}

TEST_CASE(empty_string_nodeid_needs_exactly_seven_bytes) {
    uint8_t b[16];
    opcua_nodeid_t e = str_id(0, "");
    CHECK_EQ(opcua_encode_nodeid(&e, b, 7), 7u);
    CHECK_EQ(opcua_encode_nodeid(&e, b, 6), 0u);
}

TEST_CASE(numeric_nodeid_failure_does_not_write_partial_output) {
    // 容量が足りないときは、先頭の数バイトも書かない
    struct Case {
        opcua_nodeid_t id;
        size_t need;
    };
    const Case cases[] = {{num_id(0, 7), 2},   {num_id(0, 255), 2}, {num_id(0, 256), 4},
                          {num_id(1, 1), 4},   {num_id(255, 65535), 4}, {num_id(256, 1), 7},
                          {num_id(0, 65536), 7}};
    for (const Case &c : cases) {
        for (size_t cap = 1; cap < c.need; ++cap) {
            uint8_t b[16];
            std::memset(b, 0xEE, sizeof(b));
            CHECK_EQ(opcua_encode_nodeid(&c.id, b, cap), 0u);
            for (uint8_t x : b) CHECK_EQ(x, 0xEE);
        }
        uint8_t b[16];
        CHECK_EQ(opcua_encode_nodeid(&c.id, b, c.need), c.need);
    }
}

EMBED_TEST_MAIN()
