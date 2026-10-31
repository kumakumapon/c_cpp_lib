// MQTT 3.1.1 仕様書に沿った固定バイト列 (CONNECT / PUBLISH / SUBSCRIBE / UNSUBSCRIBE / PINGREQ /
// DISCONNECT / PUBACK)、Remaining Length の境界、CONNACK / SUBACK / UNSUBACK の解析、
// クライアントの状態遷移と Keep Alive、不正入力の扱いを固定するテスト。
#include "embed_test/embed_test.hpp"
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

#include "embed_mqtt/mqtt_client.h"
#include "embed_mqtt/mqtt_packet.h"
#include "embed_mqtt/mqtt_types.h"

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
Bytes str(const char *s) { return Bytes(s, s + std::strlen(s)); }

mqtt_connect_options_t opts(const char *id) {
    mqtt_connect_options_t o{};
    o.client_id = id;
    o.clean_session = true;
    o.keep_alive_sec = 60;
    return o;
}

Bytes connect_bytes(const mqtt_connect_options_t &o, bool *ok = nullptr) {
    uint8_t buf[256];
    size_t n = 0;
    const bool r = mqtt_serialize_connect(&o, buf, sizeof(buf), &n);
    if (ok) *ok = r;
    return r ? Bytes(buf, buf + n) : Bytes{};
}

Bytes publish_bytes(const mqtt_message_t &m, bool *ok = nullptr) {
    uint8_t buf[1100];
    size_t n = 0;
    const bool r = mqtt_serialize_publish(&m, buf, sizeof(buf), &n);
    if (ok) *ok = r;
    return r ? Bytes(buf, buf + n) : Bytes{};
}

mqtt_message_t msg(const char *topic, const char *payload, mqtt_qos_t qos = MQTT_QOS_0,
                   bool retain = false, bool dup = false, uint16_t pid = 0) {
    mqtt_message_t m{};
    m.topic = topic;
    m.payload = reinterpret_cast<const uint8_t *>(payload);
    m.payload_len = payload ? std::strlen(payload) : 0;
    m.qos = qos;
    m.retain = retain;
    m.dup = dup;
    m.packet_id = pid;
    return m;
}
} // namespace

// ---- Remaining Length (MQTT 3.1.1 2.2.3) ----
TEST_CASE(remaining_length_encoding_boundaries) {
    struct Row {
        size_t value;
        Bytes bytes;
    };
    const Row rows[] = {
        {0, {0x00}},
        {1, {0x01}},
        {127, {0x7F}},
        {128, {0x80, 0x01}},
        {16383, {0xFF, 0x7F}},
        {16384, {0x80, 0x80, 0x01}},
        {2097151, {0xFF, 0xFF, 0x7F}},
        {2097152, {0x80, 0x80, 0x80, 0x01}},
        {268435455, {0xFF, 0xFF, 0xFF, 0x7F}},
    };
    for (const Row &r : rows) {
        uint8_t buf[8] = {0};
        const size_t n = mqtt_encode_remaining_length(r.value, buf);
        CHECK_EQ(n, r.bytes.size());
        CHECK(Bytes(buf, buf + n) == r.bytes);
        size_t back = 0;
        CHECK_EQ(mqtt_decode_remaining_length(r.bytes.data(), r.bytes.size(), &back), r.bytes.size());
        CHECK_EQ(back, r.value);
    }
}

TEST_CASE(remaining_length_decoding_rejects_bad_input) {
    size_t v = 777;
    const uint8_t five[] = {0x80, 0x80, 0x80, 0x80, 0x01}; // 5 バイトは不正
    CHECK_EQ(mqtt_decode_remaining_length(five, sizeof(five), &v), 0u);
    const uint8_t four_cont[] = {0xFF, 0xFF, 0xFF, 0xFF}; // 4 バイト目にも継続ビット
    CHECK_EQ(mqtt_decode_remaining_length(four_cont, sizeof(four_cont), &v), 0u);
    const uint8_t cut[] = {0x80, 0x80}; // 途中で切れている
    CHECK_EQ(mqtt_decode_remaining_length(cut, sizeof(cut), &v), 0u);
    CHECK_EQ(mqtt_decode_remaining_length(cut, 0, &v), 0u);
    CHECK_EQ(v, 777u); // 失敗時は値を書き換えない
    // 後続のバイトは読まない
    const uint8_t extra[] = {0x05, 0xFF, 0xFF};
    CHECK_EQ(mqtt_decode_remaining_length(extra, sizeof(extra), &v), 1u);
    CHECK_EQ(v, 5u);
}

// ---- CONNECT ----
TEST_CASE(connect_fixed_bytes) {
    // clean session, keep alive 60, client id "abc"
    CHECK_BYTES(connect_bytes(opts("abc")), 0x10, 0x0F, 0x00, 0x04, 'M', 'Q', 'T', 'T', 0x04, 0x02, 0x00,
                0x3C, 0x00, 0x03, 'a', 'b', 'c');
    // clean session なし
    mqtt_connect_options_t o = opts("abc");
    o.clean_session = false;
    o.keep_alive_sec = 0x1234;
    CHECK_BYTES(connect_bytes(o), 0x10, 0x0F, 0x00, 0x04, 'M', 'Q', 'T', 'T', 0x04, 0x00, 0x12, 0x34, 0x00,
                0x03, 'a', 'b', 'c');
    // keep alive 0 は既定値 60 に置き換える (この実装の仕様)
    o = opts("x");
    o.keep_alive_sec = 0;
    const Bytes b = connect_bytes(o);
    CHECK_EQ(b[10], 0x00);
    CHECK_EQ(b[11], MQTT_DEFAULT_KEEP_ALIVE);
}

TEST_CASE(connect_will_and_credentials_flags) {
    const uint8_t wm[] = {0x01, 0x02};
    mqtt_connect_options_t o = opts("id");
    o.will_topic = "w/t";
    o.will_message = wm;
    o.will_message_len = 2;
    o.will_qos = MQTT_QOS_1;
    o.will_retain = true;
    o.username = "u";
    o.password = "pw";
    // フラグ: user 0x80 | pass 0x40 | will retain 0x20 | will qos1 0x08 | will 0x04 | clean 0x02 = 0xEE
    CHECK_BYTES(connect_bytes(o), 0x10, 0x1E, 0x00, 0x04, 'M', 'Q', 'T', 'T', 0x04, 0xEE, 0x00, 0x3C, 0x00,
                0x02, 'i', 'd', 0x00, 0x03, 'w', '/', 't', 0x00, 0x02, 0x01, 0x02, 0x00, 0x01, 'u', 0x00,
                0x02, 'p', 'w');

    o.will_qos = MQTT_QOS_2;
    o.will_retain = false;
    const Bytes b2 = connect_bytes(o);
    CHECK_EQ(b2[9], 0xD6); // user | pass | will qos2 0x10 | will 0x04 | clean 0x02
    o.will_qos = MQTT_QOS_0;
    CHECK_EQ(connect_bytes(o)[9], 0xC6);

    // 遺言なし: Will 関連のフラグは立たない (トピックだけ / メッセージだけでは遺言にならない)
    mqtt_connect_options_t n = opts("id");
    n.will_qos = MQTT_QOS_2;
    n.will_retain = true;
    CHECK_EQ(connect_bytes(n)[9], 0x02);
    n.will_topic = "t";
    CHECK_EQ(connect_bytes(n)[9], 0x02);
    n.will_topic = nullptr;
    n.will_message = wm;
    n.will_message_len = 2;
    CHECK_EQ(connect_bytes(n)[9], 0x02);

    // ユーザー名だけ
    mqtt_connect_options_t u = opts("id");
    u.username = "u";
    const Bytes ub = connect_bytes(u);
    CHECK_EQ(ub[9], 0x82);
    CHECK_EQ(ub.size(), 2u + 10 + 2 + 2 + 2 + 1);
    // 空のペイロードの遺言
    mqtt_connect_options_t e = opts("id");
    e.will_topic = "t";
    e.will_message = wm;
    e.will_message_len = 0;
    const Bytes eb = connect_bytes(e);
    CHECK_EQ(eb[9], 0x06);
    CHECK_EQ(eb[1], 10 + 2 + 2 + 2 + 1 + 2);
}

TEST_CASE(connect_rejects_protocol_violations) {
    bool ok = true;
    // パスワードはユーザー名なしでは使えない [MQTT-3.1.2-22]
    mqtt_connect_options_t p = opts("id");
    p.password = "pw";
    connect_bytes(p, &ok);
    CHECK(!ok);
    // 長さ 0 のクライアント ID は clean session のときだけ [MQTT-3.1.3-7]
    mqtt_connect_options_t z = opts("");
    connect_bytes(z, &ok);
    CHECK(ok);
    z.clean_session = false;
    connect_bytes(z, &ok);
    CHECK(!ok);
    // 遺言 QoS は 0..2
    const uint8_t wm[] = {1};
    mqtt_connect_options_t w = opts("id");
    w.will_topic = "t";
    w.will_message = wm;
    w.will_message_len = 1;
    w.will_qos = (mqtt_qos_t)3;
    connect_bytes(w, &ok);
    CHECK(!ok);
    // NULL
    uint8_t buf[64];
    size_t n = 0;
    CHECK(!mqtt_serialize_connect(nullptr, buf, sizeof(buf), &n));
    mqtt_connect_options_t noid{};
    CHECK(!mqtt_serialize_connect(&noid, buf, sizeof(buf), &n));
    mqtt_connect_options_t good = opts("abc");
    CHECK(!mqtt_serialize_connect(&good, nullptr, sizeof(buf), &n));
    CHECK(!mqtt_serialize_connect(&good, buf, sizeof(buf), nullptr));
    // バッファが 1 バイト足りない / ちょうど
    CHECK(!mqtt_serialize_connect(&good, buf, 16, &n));
    CHECK(mqtt_serialize_connect(&good, buf, 17, &n));
    CHECK_EQ(n, 17u);
}

// ---- PUBLISH ----
TEST_CASE(publish_fixed_bytes) {
    CHECK_BYTES(publish_bytes(msg("a/b", "hi")), 0x30, 0x07, 0x00, 0x03, 'a', '/', 'b', 'h', 'i');
    CHECK_BYTES(publish_bytes(msg("a/b", "hi", MQTT_QOS_0, true)), 0x31, 0x07, 0x00, 0x03, 'a', '/', 'b', 'h',
                'i');
    CHECK_BYTES(publish_bytes(msg("a/b", "hi", MQTT_QOS_1, false, false, 0x1234)), 0x32, 0x09, 0x00, 0x03, 'a',
                '/', 'b', 0x12, 0x34, 'h', 'i');
    CHECK_BYTES(publish_bytes(msg("a/b", "hi", MQTT_QOS_1, false, true, 0x0001)), 0x3A, 0x09, 0x00, 0x03, 'a',
                '/', 'b', 0x00, 0x01, 'h', 'i');
    CHECK_BYTES(publish_bytes(msg("t", "x", MQTT_QOS_2, true, true, 0xABCD)), 0x3D, 0x06, 0x00, 0x01, 't', 0xAB,
                0xCD, 'x');
    // ペイロードなし
    CHECK_BYTES(publish_bytes(msg("t", nullptr)), 0x30, 0x03, 0x00, 0x01, 't');
    // QoS 0 ではパケット ID を書かない
    CHECK_BYTES(publish_bytes(msg("t", "x", MQTT_QOS_0, false, false, 0x7777)), 0x30, 0x04, 0x00, 0x01, 't', 'x');
}

TEST_CASE(publish_remaining_length_grows_with_payload) {
    const std::string big(200, 'p');
    mqtt_message_t m = msg("t", big.c_str());
    const Bytes b = publish_bytes(m);
    // 2 + 1 + 200 = 203 -> 0xCB 0x01
    CHECK_EQ(b[0], 0x30);
    CHECK_EQ(b[1], 0xCB);
    CHECK_EQ(b[2], 0x01);
    CHECK_EQ(b.size(), 1u + 2 + 203);
    // 128 バイトの境目: rem = 2 + 1 + 125 = 128 -> 2 バイト, 127 -> 1 バイト
    const std::string p125(125, 'p'), p124(124, 'p');
    CHECK_EQ(publish_bytes(msg("t", p125.c_str())).size(), 1u + 2 + 128);
    CHECK_EQ(publish_bytes(msg("t", p124.c_str())).size(), 1u + 1 + 127);
}

TEST_CASE(publish_rejects_invalid_topics_and_arguments) {
    bool ok = true;
    publish_bytes(msg("", "x"), &ok); // 長さ 0 のトピックは不可 [MQTT-4.7.3-1]
    CHECK(!ok);
    publish_bytes(msg("a/#", "x"), &ok); // ワイルドカードは発行先に使えない [MQTT-3.3.2-2]
    CHECK(!ok);
    publish_bytes(msg("a/+/b", "x"), &ok);
    CHECK(!ok);
    publish_bytes(msg("a+b", "x"), &ok);
    CHECK(!ok);
    publish_bytes(msg("a/b", "x", (mqtt_qos_t)3), &ok);
    CHECK(!ok);
    mqtt_message_t nopl = msg("t", nullptr);
    nopl.payload_len = 3; // 長さがあるのにペイロードが NULL
    publish_bytes(nopl, &ok);
    CHECK(!ok);
    uint8_t buf[64];
    size_t n = 0;
    mqtt_message_t good = msg("t", "x");
    CHECK(!mqtt_serialize_publish(nullptr, buf, sizeof(buf), &n));
    CHECK(!mqtt_serialize_publish(&good, nullptr, sizeof(buf), &n));
    CHECK(!mqtt_serialize_publish(&good, buf, sizeof(buf), nullptr));
    mqtt_message_t notopic = good;
    notopic.topic = nullptr;
    CHECK(!mqtt_serialize_publish(&notopic, buf, sizeof(buf), &n));
    // 出力バッファのちょうどの境界 (6 バイト)
    CHECK(!mqtt_serialize_publish(&good, buf, 5, &n));
    CHECK(mqtt_serialize_publish(&good, buf, 6, &n));
    CHECK_EQ(n, 6u);
    // topic_len を指定した場合はその長さだけ使う
    mqtt_message_t part = msg("abcdef", "x");
    part.topic_len = 3;
    CHECK_BYTES(publish_bytes(part), 0x30, 0x06, 0x00, 0x03, 'a', 'b', 'c', 'x');
}

TEST_CASE(publish_decode_round_trip_and_flags) {
    const mqtt_message_t cases[] = {
        msg("a/b", "hi"),
        msg("a/b", "hi", MQTT_QOS_0, true),
        msg("a/b", "hi", MQTT_QOS_1, false, true, 0x1234),
        msg("t", "", MQTT_QOS_2, true, true, 0xFFFF),
    };
    for (const mqtt_message_t &c : cases) {
        const Bytes b = publish_bytes(c);
        mqtt_message_t out{};
        CHECK_EQ(mqtt_deserialize_publish(b.data(), b.size(), &out), MQTT_OK);
        CHECK(std::string(out.topic, out.topic_len) == c.topic);
        CHECK_EQ(out.payload_len, c.payload_len);
        CHECK_EQ(out.qos, c.qos);
        CHECK_EQ(out.retain, c.retain);
        CHECK_EQ(out.dup, c.dup);
        CHECK_EQ(out.packet_id, c.qos ? c.packet_id : 0);
        if (c.payload_len) CHECK(std::memcmp(out.payload, c.payload, c.payload_len) == 0);
    }
}

TEST_CASE(publish_decode_rejects_malformed_packets) {
    mqtt_message_t out{};
    auto dec = [&](Bytes b) { return mqtt_deserialize_publish(b.data(), b.size(), &out); };
    CHECK_EQ(dec({0x30, 0x04, 0x00, 0x01, 't', 'x'}), MQTT_OK);
    CHECK_EQ(dec({0x40, 0x04, 0x00, 0x01, 't', 'x'}), MQTT_ERR_MALFORMED_PACKET);       // PUBLISH ではない
    CHECK_EQ(dec({0x36, 0x04, 0x00, 0x01, 't', 'x'}), MQTT_ERR_MALFORMED_PACKET);       // QoS 3
    CHECK_EQ(dec({0x30, 0x04, 0x00, 0x01, 't'}), MQTT_ERR_INCOMPLETE_PACKET);           // 切り詰め
    CHECK_EQ(dec({0x30, 0x04, 0x00, 0x09, 't', 'x'}), MQTT_ERR_MALFORMED_PACKET);       // トピック長がパケット外
    CHECK_EQ(dec({0x30, 0x01, 0x00}), MQTT_ERR_MALFORMED_PACKET);                       // 残り長が 2 未満
    CHECK_EQ(dec({0x32, 0x04, 0x00, 0x02, 't', 'x'}), MQTT_ERR_MALFORMED_PACKET);       // QoS1 でパケット ID がない
    CHECK_EQ(dec({0x32, 0x05, 0x00, 0x01, 't', 0x00, 0x00}), MQTT_ERR_MALFORMED_PACKET); // パケット ID 0 [MQTT-2.3.1-1]
    CHECK_EQ(dec({0x30, 0x03, 0x00, 0x00, 'x'}), MQTT_ERR_MALFORMED_PACKET);            // 長さ 0 のトピック
    CHECK_EQ(dec({0x30, 0x05, 0x00, 0x03, 'a', '#', 'b'}), MQTT_ERR_MALFORMED_PACKET);  // ワイルドカード入り
    CHECK_EQ(dec({0x30, 0x05, 0x00, 0x03, 'a', '+', 'b'}), MQTT_ERR_MALFORMED_PACKET);
    CHECK_EQ(mqtt_deserialize_publish(nullptr, 5, &out), MQTT_ERR_INVALID_PARAM);
    const uint8_t two[] = {0x30, 0x00};
    CHECK_EQ(mqtt_deserialize_publish(two, 2, &out), MQTT_ERR_INVALID_PARAM);
    CHECK_EQ(mqtt_deserialize_publish(two, 3, nullptr), MQTT_ERR_INVALID_PARAM);
}

// ---- PUBACK / SUBSCRIBE / UNSUBSCRIBE / PINGREQ / DISCONNECT ----
TEST_CASE(small_packets_fixed_bytes) {
    uint8_t buf[64];
    size_t n = 0;
    auto out = [&] { return Bytes(buf, buf + n); };

    CHECK(mqtt_serialize_puback(0x1234, buf, sizeof(buf), &n));
    CHECK_BYTES(out(), 0x40, 0x02, 0x12, 0x34);
    CHECK(mqtt_serialize_pingreq(buf, sizeof(buf), &n));
    CHECK_BYTES(out(), 0xC0, 0x00);
    CHECK(mqtt_serialize_disconnect(buf, sizeof(buf), &n));
    CHECK_BYTES(out(), 0xE0, 0x00);

    CHECK(mqtt_serialize_subscribe(0x000A, "a/+", MQTT_QOS_1, buf, sizeof(buf), &n));
    CHECK_BYTES(out(), 0x82, 0x08, 0x00, 0x0A, 0x00, 0x03, 'a', '/', '+', 0x01);
    CHECK(mqtt_serialize_subscribe(0xFFFF, "#", MQTT_QOS_2, buf, sizeof(buf), &n));
    CHECK_BYTES(out(), 0x82, 0x06, 0xFF, 0xFF, 0x00, 0x01, '#', 0x02);
    CHECK(mqtt_serialize_subscribe(1, "t", MQTT_QOS_0, buf, sizeof(buf), &n));
    CHECK_BYTES(out(), 0x82, 0x06, 0x00, 0x01, 0x00, 0x01, 't', 0x00);

    CHECK(mqtt_serialize_unsubscribe(0x0102, "a/b", buf, sizeof(buf), &n));
    CHECK_BYTES(out(), 0xA2, 0x07, 0x01, 0x02, 0x00, 0x03, 'a', '/', 'b');
}

TEST_CASE(small_packets_reject_bad_arguments) {
    uint8_t buf[64];
    size_t n = 0;
    CHECK(!mqtt_serialize_puback(1, nullptr, 4, &n));
    CHECK(!mqtt_serialize_puback(1, buf, 3, &n));
    CHECK(mqtt_serialize_puback(1, buf, 4, &n));
    CHECK(!mqtt_serialize_puback(1, buf, 4, nullptr));
    CHECK(!mqtt_serialize_pingreq(buf, 1, &n));
    CHECK(mqtt_serialize_pingreq(buf, 2, &n));
    CHECK(!mqtt_serialize_pingreq(nullptr, 2, &n));
    CHECK(!mqtt_serialize_pingreq(buf, 2, nullptr));
    CHECK(!mqtt_serialize_disconnect(buf, 1, &n));
    CHECK(mqtt_serialize_disconnect(buf, 2, &n));
    CHECK(!mqtt_serialize_disconnect(nullptr, 2, &n));

    CHECK(!mqtt_serialize_subscribe(1, nullptr, MQTT_QOS_0, buf, sizeof(buf), &n));
    CHECK(!mqtt_serialize_subscribe(1, "", MQTT_QOS_0, buf, sizeof(buf), &n)); // 空のフィルタは不可 [MQTT-4.7.3-1]
    CHECK(!mqtt_serialize_subscribe(1, "t", (mqtt_qos_t)3, buf, sizeof(buf), &n));
    CHECK(!mqtt_serialize_subscribe(0, "t", MQTT_QOS_0, buf, sizeof(buf), &n)); // パケット ID 0 は不可
    CHECK(!mqtt_serialize_subscribe(1, "t", MQTT_QOS_0, buf, 7, &n));
    CHECK(mqtt_serialize_subscribe(1, "t", MQTT_QOS_0, buf, 8, &n));
    CHECK(!mqtt_serialize_unsubscribe(1, nullptr, buf, sizeof(buf), &n));
    CHECK(!mqtt_serialize_unsubscribe(1, "", buf, sizeof(buf), &n));
    CHECK(!mqtt_serialize_unsubscribe(0, "t", buf, sizeof(buf), &n));
    CHECK(!mqtt_serialize_unsubscribe(1, "t", buf, 6, &n));
    CHECK(mqtt_serialize_unsubscribe(1, "t", buf, 7, &n));
}

// ---- CONNACK / PUBACK / SUBACK / UNSUBACK の解析 ----
TEST_CASE(connack_return_codes_and_session_present) {
    for (uint8_t rc = 0; rc <= 5; ++rc) {
        for (uint8_t sp : {uint8_t(0), uint8_t(1)}) {
            const uint8_t pkt[] = {0x20, 0x02, sp, rc};
            bool present = !sp;
            mqtt_connack_rc_t got = MQTT_CONNACK_SERVER_UNAVAILABLE;
            CHECK_EQ(mqtt_deserialize_connack(pkt, 4, &present, &got), MQTT_OK);
            CHECK_EQ(present, sp == 1);
            CHECK_EQ((int)got, (int)rc);
        }
    }
}

TEST_CASE(connack_rejects_malformed_packets) {
    bool sp = false;
    mqtt_connack_rc_t rc = MQTT_CONNACK_ACCEPTED;
    auto dec = [&](Bytes b) { return mqtt_deserialize_connack(b.data(), b.size(), &sp, &rc); };
    CHECK_EQ(dec({0x20, 0x02, 0x00, 0x00}), MQTT_OK);
    CHECK_EQ(dec({0x30, 0x02, 0x00, 0x00}), MQTT_ERR_MALFORMED_PACKET); // 種別違い
    CHECK_EQ(dec({0x21, 0x02, 0x00, 0x00}), MQTT_ERR_MALFORMED_PACKET); // 予約フラグ [MQTT-2.2.2-2]
    CHECK_EQ(dec({0x20, 0x03, 0x00, 0x00}), MQTT_ERR_MALFORMED_PACKET); // 残り長
    CHECK_EQ(dec({0x20, 0x01, 0x00, 0x00}), MQTT_ERR_MALFORMED_PACKET);
    CHECK_EQ(dec({0x20, 0x02, 0x02, 0x00}), MQTT_ERR_MALFORMED_PACKET); // 予約ビット [MQTT-3.2.2-1]
    CHECK_EQ(dec({0x20, 0x02, 0x00, 0x06}), MQTT_ERR_MALFORMED_PACKET); // 6 以上は予約
    CHECK_EQ(dec({0x20, 0x02, 0x00, 0xFF}), MQTT_ERR_MALFORMED_PACKET);
    const uint8_t three[] = {0x20, 0x02, 0x00};
    CHECK_EQ(mqtt_deserialize_connack(three, 3, &sp, &rc), MQTT_ERR_INVALID_PARAM);
    CHECK_EQ(mqtt_deserialize_connack(nullptr, 4, &sp, &rc), MQTT_ERR_INVALID_PARAM);
}

TEST_CASE(ack_packets_decode_and_validate) {
    uint16_t id = 0;
    auto pa = [&](Bytes b) { return mqtt_deserialize_puback(b.data(), b.size(), &id); };
    CHECK_EQ(pa({0x40, 0x02, 0x12, 0x34}), MQTT_OK);
    CHECK_EQ(id, 0x1234u);
    CHECK_EQ(pa({0x41, 0x02, 0x00, 0x01}), MQTT_ERR_MALFORMED_PACKET);
    CHECK_EQ(pa({0x50, 0x02, 0x00, 0x01}), MQTT_ERR_MALFORMED_PACKET);
    CHECK_EQ(pa({0x40, 0x03, 0x00, 0x01}), MQTT_ERR_MALFORMED_PACKET);
    CHECK_EQ(mqtt_deserialize_puback(nullptr, 4, &id), MQTT_ERR_INVALID_PARAM);
    const uint8_t t3[] = {0x40, 0x02, 0x00};
    CHECK_EQ(mqtt_deserialize_puback(t3, 3, &id), MQTT_ERR_INVALID_PARAM);

    auto un = [&](Bytes b) { return mqtt_deserialize_unsuback(b.data(), b.size(), &id); };
    CHECK_EQ(un({0xB0, 0x02, 0xAB, 0xCD}), MQTT_OK);
    CHECK_EQ(id, 0xABCDu);
    CHECK_EQ(un({0xB1, 0x02, 0x00, 0x01}), MQTT_ERR_MALFORMED_PACKET);
    CHECK_EQ(un({0x40, 0x02, 0x00, 0x01}), MQTT_ERR_MALFORMED_PACKET);
    CHECK_EQ(un({0xB0, 0x01, 0x00, 0x01}), MQTT_ERR_MALFORMED_PACKET);
    CHECK_EQ(mqtt_deserialize_unsuback(nullptr, 4, &id), MQTT_ERR_INVALID_PARAM);

    mqtt_qos_t q = MQTT_QOS_0;
    auto sa = [&](Bytes b) { return mqtt_deserialize_suback(b.data(), b.size(), &id, &q); };
    for (uint8_t g : {uint8_t(0), uint8_t(1), uint8_t(2), uint8_t(0x80)}) { // 0x80 は失敗
        CHECK_EQ(sa({0x90, 0x03, 0x00, 0x07, g}), MQTT_OK);
        CHECK_EQ(id, 7u);
        CHECK_EQ((int)q, (int)g);
    }
    CHECK_EQ(sa({0x90, 0x03, 0x00, 0x07, 0x03}), MQTT_ERR_MALFORMED_PACKET); // 許可されない値
    CHECK_EQ(sa({0x90, 0x03, 0x00, 0x07, 0x7F}), MQTT_ERR_MALFORMED_PACKET);
    CHECK_EQ(sa({0x91, 0x03, 0x00, 0x07, 0x00}), MQTT_ERR_MALFORMED_PACKET);
    CHECK_EQ(sa({0xA0, 0x03, 0x00, 0x07, 0x00}), MQTT_ERR_MALFORMED_PACKET);
    CHECK_EQ(sa({0x90, 0x04, 0x00, 0x07, 0x00}), MQTT_ERR_MALFORMED_PACKET);
    const uint8_t t4[] = {0x90, 0x03, 0x00, 0x07};
    CHECK_EQ(mqtt_deserialize_suback(t4, 4, &id, &q), MQTT_ERR_INVALID_PARAM);
}

TEST_CASE(peek_packet_info_reports_type_and_total_length) {
    mqtt_packet_type_t t = MQTT_PACKET_TYPE_RESERVED;
    size_t total = 0;
    const uint8_t full[] = {0x30, 0x04, 0x00, 0x01, 't', 'x', 0xEE};
    CHECK_EQ(mqtt_peek_packet_info(full, sizeof(full), &t, &total), MQTT_OK);
    CHECK_EQ(t, MQTT_PACKET_TYPE_PUBLISH);
    CHECK_EQ(total, 6u); // 後ろの余計なバイトは含めない
    CHECK_EQ(mqtt_peek_packet_info(full, 6, &t, &total), MQTT_OK);
    CHECK_EQ(mqtt_peek_packet_info(full, 5, &t, &total), MQTT_ERR_INCOMPLETE_PACKET);
    CHECK_EQ(mqtt_peek_packet_info(full, 2, &t, &total), MQTT_ERR_INCOMPLETE_PACKET);
    CHECK_EQ(mqtt_peek_packet_info(full, 1, &t, &total), MQTT_ERR_INCOMPLETE_PACKET);
    const uint8_t ping[] = {0xC0, 0x00};
    CHECK_EQ(mqtt_peek_packet_info(ping, 2, &t, &total), MQTT_OK);
    CHECK_EQ(t, MQTT_PACKET_TYPE_PINGREQ);
    CHECK_EQ(total, 2u);
    const uint8_t longrl[] = {0x30, 0x80, 0x01}; // 残り長 128: 本体が届くまで未完
    CHECK_EQ(mqtt_peek_packet_info(longrl, 3, &t, &total), MQTT_ERR_INCOMPLETE_PACKET);
    const uint8_t cutrl[] = {0x30, 0x80, 0x80};  // 残り長のバイトが途中で切れている
    CHECK_EQ(mqtt_peek_packet_info(cutrl, 3, &t, &total), MQTT_ERR_INCOMPLETE_PACKET);
    const uint8_t bad[] = {0x30, 0xFF, 0xFF, 0xFF, 0xFF, 0x01}; // 5 バイトの残り長は不正
    CHECK_EQ(mqtt_peek_packet_info(bad, 6, &t, &total), MQTT_ERR_MALFORMED_PACKET);
    const uint8_t bad4[] = {0x30, 0xFF, 0xFF, 0xFF, 0xFF};      // 4 バイト目にも継続ビット
    CHECK_EQ(mqtt_peek_packet_info(bad4, 5, &t, &total), MQTT_ERR_MALFORMED_PACKET);
    CHECK_EQ(mqtt_peek_packet_info(nullptr, 2, &t, &total), MQTT_ERR_INCOMPLETE_PACKET);
}

// ---- クライアント ----
namespace {
struct Wire {
    std::vector<Bytes> sent;
    int connects = 0, pubacks = 0, subacks = 0;
    mqtt_connack_rc_t last_rc = MQTT_CONNACK_ACCEPTED;
    bool last_sp = false;
    std::vector<std::string> topics, payloads;
    std::vector<uint16_t> ack_ids;
    mqtt_qos_t last_granted = MQTT_QOS_0;
};
void tx(const uint8_t *d, size_t n, void *u) { static_cast<Wire *>(u)->sent.emplace_back(d, d + n); }
void on_conn(mqtt_client_t *, mqtt_connack_rc_t rc, bool sp, void *u) {
    Wire *w = static_cast<Wire *>(u);
    ++w->connects;
    w->last_rc = rc;
    w->last_sp = sp;
}
void on_msg(mqtt_client_t *, const mqtt_message_t *m, void *u) {
    Wire *w = static_cast<Wire *>(u);
    w->topics.emplace_back(m->topic, m->topic_len);
    w->payloads.emplace_back(reinterpret_cast<const char *>(m->payload), m->payload_len);
}
void on_ack(mqtt_client_t *, uint16_t id, void *u) {
    Wire *w = static_cast<Wire *>(u);
    ++w->pubacks;
    w->ack_ids.push_back(id);
}
void on_sub(mqtt_client_t *, uint16_t id, mqtt_qos_t g, void *u) {
    Wire *w = static_cast<Wire *>(u);
    ++w->subacks;
    w->ack_ids.push_back(id);
    w->last_granted = g;
}

struct Client {
    Wire wire;
    mqtt_client_t c;
    Client() {
        mqtt_client_init(&c, tx, &wire);
        mqtt_client_set_callbacks(&c, on_conn, on_msg, on_ack, on_sub);
    }
    void connect_ok(uint16_t keep_alive = 60) {
        mqtt_connect_options_t o = opts("cid");
        o.keep_alive_sec = keep_alive;
        REQUIRE(mqtt_client_connect(&c, &o));
        const uint8_t ack[] = {0x20, 0x02, 0x00, 0x00};
        mqtt_client_feed_bytes(&c, ack, sizeof(ack));
        REQUIRE(c.state == MQTT_STATE_CONNECTED);
        wire.sent.clear();
    }
    void feed(const Bytes &b) { mqtt_client_feed_bytes(&c, b.data(), b.size()); }
};
} // namespace

TEST_CASE(client_init_and_connect_state_machine) {
    Client k;
    CHECK_EQ(k.c.state, MQTT_STATE_DISCONNECTED);
    CHECK_EQ(k.c.next_packet_id, 1u);
    CHECK_EQ(k.c.keep_alive_sec, (uint16_t)MQTT_DEFAULT_KEEP_ALIVE);
    CHECK_EQ(k.c.rx_len, 0u);

    mqtt_connect_options_t o = opts("cid");
    o.keep_alive_sec = 30;
    CHECK(mqtt_client_connect(&k.c, &o));
    CHECK_EQ(k.c.state, MQTT_STATE_CONNECTING);
    CHECK_EQ(k.c.keep_alive_sec, 30u);
    REQUIRE(k.wire.sent.size() == 1u);
    CHECK_EQ(k.wire.sent[0][0], 0x10);

    // 接続処理中・接続済みの再 CONNECT は拒否する [MQTT-3.1.0-2]
    CHECK(!mqtt_client_connect(&k.c, &o));
    CHECK_EQ(k.wire.sent.size(), 1u);

    k.feed({0x20, 0x02, 0x01, 0x00}); // session present
    CHECK_EQ(k.c.state, MQTT_STATE_CONNECTED);
    CHECK_EQ(k.wire.connects, 1);
    CHECK_EQ(k.wire.last_rc, MQTT_CONNACK_ACCEPTED);
    CHECK(k.wire.last_sp);
    CHECK(!mqtt_client_connect(&k.c, &o));

    CHECK(mqtt_client_disconnect(&k.c));
    CHECK_EQ(k.c.state, MQTT_STATE_DISCONNECTED);
    CHECK_BYTES(k.wire.sent.back(), 0xE0, 0x00);
    // 切断後は再接続できる
    CHECK(mqtt_client_connect(&k.c, &o));
}

TEST_CASE(client_refused_connection_returns_to_disconnected) {
    for (uint8_t rc = 1; rc <= 5; ++rc) {
        Client k;
        mqtt_connect_options_t o = opts("cid");
        mqtt_client_connect(&k.c, &o);
        k.feed({0x20, 0x02, 0x00, rc});
        CHECK_EQ(k.c.state, MQTT_STATE_DISCONNECTED);
        CHECK_EQ(k.wire.connects, 1);
        CHECK_EQ((int)k.wire.last_rc, (int)rc);
        CHECK(!k.wire.last_sp);
    }
}

TEST_CASE(client_operations_require_a_connection) {
    Client k;
    CHECK(!mqtt_client_publish(&k.c, "t", "x", 1, MQTT_QOS_0, false));
    CHECK(!mqtt_client_subscribe(&k.c, "t", MQTT_QOS_0));
    CHECK(!mqtt_client_unsubscribe(&k.c, "t"));
    CHECK(!mqtt_client_ping(&k.c));
    CHECK(!mqtt_client_disconnect(&k.c));
    CHECK(k.wire.sent.empty());
    CHECK_EQ(k.c.next_packet_id, 1u); // 拒否した操作で ID を消費しない

    // 接続処理中 (CONNACK 待ち) でも続けて送ってよい [MQTT-3.1.4]
    mqtt_connect_options_t o = opts("cid");
    mqtt_client_connect(&k.c, &o);
    CHECK(mqtt_client_publish(&k.c, "t", "x", 1, MQTT_QOS_0, false));

    // 引数の不備
    Client c2;
    c2.connect_ok();
    CHECK(!mqtt_client_publish(nullptr, "t", "x", 1, MQTT_QOS_0, false));
    CHECK(!mqtt_client_publish(&c2.c, nullptr, "x", 1, MQTT_QOS_0, false));
    CHECK(!mqtt_client_publish(&c2.c, "", "x", 1, MQTT_QOS_0, false));
    CHECK(!mqtt_client_publish(&c2.c, "a/#", "x", 1, MQTT_QOS_0, false));
    CHECK(!mqtt_client_subscribe(&c2.c, nullptr, MQTT_QOS_0));
    CHECK(!mqtt_client_subscribe(&c2.c, "", MQTT_QOS_0));
    CHECK(!mqtt_client_unsubscribe(&c2.c, nullptr));
    CHECK(!mqtt_client_connect(nullptr, &o));
    CHECK(!mqtt_client_connect(&c2.c, nullptr));
    CHECK(c2.wire.sent.empty());
    mqtt_client_t nosend;
    mqtt_client_init(&nosend, nullptr, nullptr);
    CHECK(!mqtt_client_connect(&nosend, &o));
    CHECK(!mqtt_client_publish(&nosend, "t", "x", 1, MQTT_QOS_0, false));
    CHECK(!mqtt_client_ping(&nosend));
}

TEST_CASE(client_sends_exact_bytes_and_allocates_packet_ids) {
    Client k;
    k.connect_ok();
    CHECK(mqtt_client_publish(&k.c, "a/b", "hi", 2, MQTT_QOS_0, true));
    CHECK_BYTES(k.wire.sent.back(), 0x31, 0x07, 0x00, 0x03, 'a', '/', 'b', 'h', 'i');
    CHECK(mqtt_client_publish(&k.c, "a/b", "hi", 2, MQTT_QOS_1, false)); // ID 1
    CHECK_BYTES(k.wire.sent.back(), 0x32, 0x09, 0x00, 0x03, 'a', '/', 'b', 0x00, 0x01, 'h', 'i');
    CHECK(mqtt_client_subscribe(&k.c, "x/+", MQTT_QOS_1));                 // ID 2
    CHECK_BYTES(k.wire.sent.back(), 0x82, 0x08, 0x00, 0x02, 0x00, 0x03, 'x', '/', '+', 0x01);
    CHECK(mqtt_client_unsubscribe(&k.c, "x/+"));                           // ID 3
    CHECK_BYTES(k.wire.sent.back(), 0xA2, 0x07, 0x00, 0x03, 0x00, 0x03, 'x', '/', '+');
    CHECK(mqtt_client_publish(&k.c, "t", nullptr, 0, MQTT_QOS_2, false));  // ID 4
    CHECK_BYTES(k.wire.sent.back(), 0x34, 0x05, 0x00, 0x01, 't', 0x00, 0x04);
    CHECK(mqtt_client_ping(&k.c));
    CHECK_BYTES(k.wire.sent.back(), 0xC0, 0x00);
    // QoS 0 は ID を消費しない
    mqtt_client_publish(&k.c, "t", "x", 1, MQTT_QOS_0, false);
    CHECK_EQ(k.c.next_packet_id, 5u);
}

TEST_CASE(client_publish_size_limit) {
    Client k;
    k.connect_ok();
    // 最大パケット 1024 バイト: ヘッダ 1 + 残り長 2 + トピック 2+1 + ペイロード → ペイロードは 1018 まで
    std::string ok(1018, 'p'), over(1019, 'p');
    CHECK(mqtt_client_publish(&k.c, "t", ok.data(), ok.size(), MQTT_QOS_0, false));
    CHECK_EQ(k.wire.sent.back().size(), 1024u);
    k.wire.sent.clear();
    CHECK(!mqtt_client_publish(&k.c, "t", over.data(), over.size(), MQTT_QOS_0, false));
    CHECK(k.wire.sent.empty());
}

TEST_CASE(client_dispatches_incoming_packets) {
    Client k;
    k.connect_ok();
    // QoS0 の PUBLISH
    k.feed({0x30, 0x07, 0x00, 0x03, 'a', '/', 'b', 'h', 'i'});
    REQUIRE(k.wire.topics.size() == 1u);
    CHECK(k.wire.topics[0] == "a/b");
    CHECK(k.wire.payloads[0] == "hi");
    CHECK(k.wire.sent.empty()); // QoS0 には応答しない
    // QoS1 の PUBLISH には PUBACK を返す
    k.feed({0x32, 0x09, 0x00, 0x03, 'a', '/', 'b', 0x12, 0x34, 'h', 'i'});
    CHECK_EQ(k.wire.topics.size(), 2u);
    REQUIRE(k.wire.sent.size() == 1u);
    CHECK_BYTES(k.wire.sent[0], 0x40, 0x02, 0x12, 0x34);
    // PUBACK / SUBACK
    k.feed({0x40, 0x02, 0x00, 0x09});
    CHECK_EQ(k.wire.pubacks, 1);
    CHECK_EQ(k.wire.ack_ids.back(), 9u);
    k.feed({0x90, 0x03, 0x00, 0x05, 0x01});
    CHECK_EQ(k.wire.subacks, 1);
    CHECK_EQ(k.wire.ack_ids.back(), 5u);
    CHECK_EQ(k.wire.last_granted, MQTT_QOS_1);
    k.feed({0x90, 0x03, 0x00, 0x06, 0x80}); // 購読失敗
    CHECK_EQ(k.wire.subacks, 2);
    CHECK_EQ((int)k.wire.last_granted, 0x80);
    // PINGRESP, UNSUBACK は静かに受け取る
    k.feed({0xD0, 0x00});
    k.feed({0xB0, 0x02, 0x00, 0x05});
    CHECK_EQ(k.c.rx_len, 0u);
    // 壊れたパケットは通知せず、後続を処理できる
    k.feed({0x40, 0x03, 0x00, 0x01, 0x02});
    CHECK_EQ(k.wire.pubacks, 1);
    k.feed({0x40, 0x02, 0x00, 0x0A});
    CHECK_EQ(k.wire.pubacks, 2);
}

TEST_CASE(client_reassembles_streams) {
    Client k;
    k.connect_ok();
    const Bytes a = {0x30, 0x07, 0x00, 0x03, 'a', '/', 'b', 'h', 'i'};
    const Bytes b = {0x30, 0x05, 0x00, 0x03, 'c', '/', 'd'};
    // 1 バイトずつ
    for (uint8_t x : a) mqtt_client_feed_bytes(&k.c, &x, 1);
    CHECK_EQ(k.wire.topics.size(), 1u);
    CHECK_EQ(k.c.rx_len, 0u);
    // 2 つのパケットが 1 回で届く + 3 つ目の前半
    Bytes both = cat({a, b, Bytes{0x30, 0x04}});
    k.feed(both);
    CHECK_EQ(k.wire.topics.size(), 3u);
    CHECK(k.wire.topics[1] == "a/b");
    CHECK(k.wire.topics[2] == "c/d");
    CHECK_EQ(k.c.rx_len, 2u); // 前半が残る
    k.feed({0x00, 0x01, 't', 'z'});
    CHECK_EQ(k.wire.topics.size(), 4u);
    CHECK(k.wire.topics[3] == "t");
    CHECK(k.wire.payloads[3] == "z");
    CHECK_EQ(k.c.rx_len, 0u);
    // 空の入力は無視
    mqtt_client_feed_bytes(&k.c, a.data(), 0);
    mqtt_client_feed_bytes(&k.c, nullptr, 3);
    mqtt_client_feed_bytes(nullptr, a.data(), 3);
    CHECK_EQ(k.wire.topics.size(), 4u);
}

TEST_CASE(client_receive_buffer_overflow_and_bad_length_do_not_wedge_the_stream) {
    Client k;
    k.connect_ok();
    // バッファより大きい塊はまるごと捨てる (状態は壊さない)
    Bytes huge(MQTT_MAX_PACKET_SIZE + 1, 0x00);
    k.feed(huge);
    CHECK_EQ(k.c.rx_len, 0u);
    // 5 バイトの残り長 (不正) を受けたら受信バッファを捨て、以降のパケットを処理できる
    k.feed({0x30, 0xFF, 0xFF, 0xFF, 0xFF, 0x01});
    CHECK_EQ(k.c.rx_len, 0u);
    k.feed({0x30, 0x05, 0x00, 0x03, 'a', '/', 'b'});
    CHECK_EQ(k.wire.topics.size(), 1u);
}

TEST_CASE(client_keep_alive_pings_and_activity) {
    Client k;
    k.connect_ok(10);
    CHECK_EQ(k.c.keep_alive_sec, 10u);
    mqtt_client_yield(&k.c, 0);
    mqtt_client_yield(&k.c, 9);
    CHECK(k.wire.sent.empty());
    mqtt_client_yield(&k.c, 10); // ちょうど Keep Alive に達したら PINGREQ
    REQUIRE(k.wire.sent.size() == 1u);
    CHECK_BYTES(k.wire.sent[0], 0xC0, 0x00);
    mqtt_client_yield(&k.c, 19);
    CHECK_EQ(k.wire.sent.size(), 1u);
    mqtt_client_yield(&k.c, 20);
    CHECK_EQ(k.wire.sent.size(), 2u);

    // 他のパケットを送っていれば、その時点から数え直す
    k.wire.sent.clear();
    mqtt_client_yield(&k.c, 25);
    mqtt_client_publish(&k.c, "t", "x", 1, MQTT_QOS_0, false);
    k.wire.sent.clear();
    mqtt_client_yield(&k.c, 26);   // 送信を記録
    mqtt_client_yield(&k.c, 35);
    CHECK(k.wire.sent.empty());
    mqtt_client_yield(&k.c, 36);
    CHECK_EQ(k.wire.sent.size(), 1u);
    CHECK_BYTES(k.wire.sent[0], 0xC0, 0x00);

    // 接続前・切断後は何も送らない
    Client idle;
    mqtt_client_yield(&idle.c, 1000);
    mqtt_client_yield(nullptr, 1000);
    CHECK(idle.wire.sent.empty());
    k.wire.sent.clear();
    mqtt_client_disconnect(&k.c);
    k.wire.sent.clear();
    mqtt_client_yield(&k.c, 5000);
    CHECK(k.wire.sent.empty());
}

TEST_CASE(client_first_ping_is_relative_to_connection_not_to_zero) {
    // 起動から長い時間が経ってから接続しても、接続直後に PINGREQ を出さない
    Client k;
    mqtt_connect_options_t o = opts("cid");
    o.keep_alive_sec = 60;
    mqtt_client_connect(&k.c, &o);
    k.feed({0x20, 0x02, 0x00, 0x00});
    k.wire.sent.clear();
    mqtt_client_yield(&k.c, 100000);
    CHECK(k.wire.sent.empty());
    mqtt_client_yield(&k.c, 100059);
    CHECK(k.wire.sent.empty());
    mqtt_client_yield(&k.c, 100060);
    CHECK_EQ(k.wire.sent.size(), 1u);
}

EMBED_TEST_MAIN()
