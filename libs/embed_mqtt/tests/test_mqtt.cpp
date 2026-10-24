#include "embed_test/embed_test.hpp"
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "embed_mqtt/embed_mqtt.hpp"
#include "embed_mqtt/mqtt_client.h"
#include "embed_mqtt/mqtt_packet.h"
#include "embed_mqtt/mqtt_topic.h"
#include "embed_mqtt/mqtt_types.h"

TEST_CASE(test_topic_matching) {
    std::cout << "# [Test] Running MQTT topic matching tests..." << std::endl;

    // 完全一致
    CHECK(mqtt_topic_match("sensor/temperature", "sensor/temperature") == true);
    CHECK(mqtt_topic_match("sensor/temperature", "sensor/humidity") == false);

    // 単一レベルワイルドカード '+'
    CHECK(mqtt_topic_match("sensor/+/temperature", "sensor/living/temperature") == true);
    CHECK(mqtt_topic_match("sensor/+/temperature", "sensor/kitchen/temperature") == true);
    CHECK(mqtt_topic_match("sensor/+/temperature", "sensor/living/room1/temperature") == false);

    // 複数レベルワイルドカード '#'
    CHECK(mqtt_topic_match("sensor/#", "sensor/living/temperature") == true);
    CHECK(mqtt_topic_match("sensor/#", "sensor/a/b/c/d") == true);
    CHECK(mqtt_topic_match("#", "any/topic/name") == true);
    CHECK(mqtt_topic_match("device/#", "sensor/living") == false);

    std::cout << "# [Test] MQTT topic matching passed!" << std::endl;
}

TEST_CASE(test_packet_encode_decode) {
    std::cout << "# [Test] Running packet serialization tests..." << std::endl;

    // 1. CONNECT パケット作成
    mqtt_connect_options_t opts{};
    opts.client_id = "test_client_01";
    opts.clean_session = true;
    opts.keep_alive_sec = 30;

    uint8_t buf[256];
    size_t out_len = 0;
    bool ok = mqtt_serialize_connect(&opts, buf, sizeof(buf), &out_len);
    CHECK(ok);
    CHECK(out_len > 0);

    // 2. PUBLISH パケット (QoS 1)
    mqtt_message_t pub_msg{};
    pub_msg.topic = "home/sensor/temp";
    pub_msg.payload = (const uint8_t *)"25.4";
    pub_msg.payload_len = 4;
    pub_msg.qos = MQTT_QOS_1;
    pub_msg.packet_id = 100;
    pub_msg.retain = false;

    ok = mqtt_serialize_publish(&pub_msg, buf, sizeof(buf), &out_len);
    CHECK(ok);

    // デシリアライズ検証
    mqtt_message_t dec_msg{};
    mqtt_status_t st = mqtt_deserialize_publish(buf, out_len, &dec_msg);
    CHECK(st == MQTT_OK);
    CHECK(dec_msg.qos == MQTT_QOS_1);
    CHECK(dec_msg.packet_id == 100);
    CHECK(dec_msg.topic_len == strlen("home/sensor/temp"));
    CHECK(memcmp(dec_msg.topic, "home/sensor/temp", dec_msg.topic_len) == 0);
    CHECK(dec_msg.payload_len == 4);
    CHECK(memcmp(dec_msg.payload, "25.4", 4) == 0);

    std::cout << "# [Test] Packet serialization passed!" << std::endl;
}

TEST_CASE(test_cpp_client_loopback) {
    std::cout << "# [Test] Running C++ Client loopback tests..." << std::endl;

    std::vector<uint8_t> broker_rx_pipe;
    std::vector<uint8_t> client_rx_pipe;

    embed_mqtt::Client client;

    // 送信データを仮想ブローカーへ
    client.set_send_handler([&broker_rx_pipe](const uint8_t *data, size_t len) {
        broker_rx_pipe.insert(broker_rx_pipe.end(), data, data + len);
    });

    bool connected_flag = false;
    client.set_connect_handler([&connected_flag](mqtt_connack_rc_t rc, bool) {
        if (rc == MQTT_CONNACK_ACCEPTED) {
            connected_flag = true;
        }
    });

    std::string rx_topic;
    std::string rx_payload;
    client.set_message_handler(
        [&rx_topic, &rx_payload](const std::string &t, const std::vector<uint8_t> &p, mqtt_qos_t) {
            rx_topic = t;
            rx_payload.assign(p.begin(), p.end());
        });

    // 1. Connect
    client.connect("embed_node_01");
    CHECK(!broker_rx_pipe.empty());

    // 仮想ブローカー: CONNACK を返信
    uint8_t connack_pkt[] = {0x20, 0x02, 0x00, 0x00}; // CONNACK: Accepted
    client.feed_bytes(connack_pkt, sizeof(connack_pkt));
    CHECK(connected_flag == true);
    CHECK(client.is_connected() == true);

    // 2. ブローカーから PUBLISH 受信 (トピック "device/status", ペイロード "ONLINE")
    mqtt_message_t incoming_pub{};
    incoming_pub.topic = "device/status";
    incoming_pub.payload = (const uint8_t *)"ONLINE";
    incoming_pub.payload_len = 6;
    incoming_pub.qos = MQTT_QOS_0;

    uint8_t pub_buf[64];
    size_t pub_len = 0;
    mqtt_serialize_publish(&incoming_pub, pub_buf, sizeof(pub_buf), &pub_len);

    client.feed_bytes(pub_buf, pub_len);

    CHECK(rx_topic == "device/status");
    CHECK(rx_payload == "ONLINE");

    std::cout << "# [Test] C++ Client loopback passed!" << std::endl;
}


// ---- 回帰テスト: 境界検査 (レビュー指摘) ----
TEST_CASE(regression_connect_rejects_oversized_fields) {
    std::string long_id(3000, 'a');
    mqtt_connect_options_t opts{};
    opts.client_id = long_id.c_str();
    opts.clean_session = true;
    uint8_t buf[1024];
    size_t out_len = 0;
    CHECK(!mqtt_serialize_connect(&opts, buf, sizeof(buf), &out_len));

    // 十分な出力バッファがあれば長い ID でも正しくエンコードできる
    std::vector<uint8_t> big(4096);
    CHECK(mqtt_serialize_connect(&opts, big.data(), big.size(), &out_len));
    mqtt_packet_type_t t;
    size_t total = 0;
    CHECK(mqtt_peek_packet_info(big.data(), out_len, &t, &total) == MQTT_OK);
    CHECK(total == out_len);

    // will メッセージ長が出力バッファを超える
    opts.client_id = "c";
    opts.will_topic = "t";
    uint8_t will[2000] = {};
    opts.will_message = will;
    opts.will_message_len = sizeof(will);
    CHECK(!mqtt_serialize_connect(&opts, buf, sizeof(buf), &out_len));
}

TEST_CASE(regression_serializers_respect_buffer_size) {
    mqtt_message_t m{};
    m.topic = "a/b";
    m.payload = (const uint8_t *)"hello";
    m.payload_len = 5;
    size_t n = 0;
    // 必要長は 1 + 1 + 2 + 3 + 5 = 12。11 以下は拒否し、バッファ外を書かない
    std::vector<uint8_t> exact(12, 0xEE);
    CHECK(mqtt_serialize_publish(&m, exact.data(), exact.size(), &n));
    CHECK(n == 12);
    for (size_t sz = 0; sz < 12; ++sz) {
        std::vector<uint8_t> small(sz + 1, 0xEE); // 番兵 1 バイト
        CHECK(!mqtt_serialize_publish(&m, small.data(), sz, &n));
        CHECK(small[sz] == 0xEE);
    }
    for (size_t sz = 0; sz < 10; ++sz) {
        std::vector<uint8_t> small(sz + 1, 0xEE);
        CHECK(!mqtt_serialize_subscribe(1, "a/b", MQTT_QOS_1, small.data(), sz, &n));
        if (sz < 9) CHECK(!mqtt_serialize_unsubscribe(1, "a/b", small.data(), sz, &n));
        CHECK(small[sz] == 0xEE);
    }
    CHECK(!mqtt_serialize_subscribe(1, "a/b", (mqtt_qos_t)3, exact.data(), exact.size(), &n));
}

TEST_CASE(regression_deserialize_publish_stays_inside_packet) {
    // Remaining Length=2 のパケットが topic_len=16 を主張 (後続バイトを読んではならない)
    uint8_t b[64];
    memset(b, 'X', sizeof(b));
    b[0] = 0x30;
    b[1] = 0x02;
    b[2] = 0x00;
    b[3] = 0x10;
    mqtt_message_t m{};
    CHECK(mqtt_deserialize_publish(b, 20, &m) == MQTT_ERR_MALFORMED_PACKET);

    // Remaining Length=0 / 1: topic 長フィールドすら無い
    std::vector<uint8_t> tiny = {0x30, 0x00, 0x00};
    CHECK(mqtt_deserialize_publish(tiny.data(), tiny.size(), &m) == MQTT_ERR_MALFORMED_PACKET);
    std::vector<uint8_t> one = {0x30, 0x01, 0xAA};
    CHECK(mqtt_deserialize_publish(one.data(), one.size(), &m) == MQTT_ERR_MALFORMED_PACKET);

    // QoS 1 でパケット ID がパケット内に収まらない
    std::vector<uint8_t> noid = {0x32, 0x03, 0x00, 0x01, 'a', 0x00, 0x01};
    CHECK(mqtt_deserialize_publish(noid.data(), noid.size(), &m) == MQTT_ERR_MALFORMED_PACKET);

    // 不正 QoS 3
    std::vector<uint8_t> q3 = {0x36, 0x05, 0x00, 0x01, 'a', 0x00, 0x01};
    CHECK(mqtt_deserialize_publish(q3.data(), q3.size(), &m) == MQTT_ERR_MALFORMED_PACKET);

    // 正常: payload_len はパケット内の残りになる
    std::vector<uint8_t> ok = {0x30, 0x06, 0x00, 0x01, 'a', 'x', 'y', 'z', 0x99};
    CHECK(mqtt_deserialize_publish(ok.data(), ok.size() - 1, &m) == MQTT_OK);
    CHECK(m.payload_len == 3);
}

TEST_CASE(regression_ack_deserializers_validate_length) {
    uint16_t id = 0;
    mqtt_qos_t q = MQTT_QOS_0;
    std::vector<uint8_t> suback = {0x90, 0x03, 0x00, 0x07, 0x01};
    CHECK(mqtt_deserialize_suback(suback.data(), suback.size(), &id, &q) == MQTT_OK);
    CHECK(id == 7);
    CHECK(q == MQTT_QOS_1);
    suback[1] = 0x09; // 不整合な Remaining Length
    CHECK(mqtt_deserialize_suback(suback.data(), suback.size(), &id, &q) == MQTT_ERR_MALFORMED_PACKET);
    std::vector<uint8_t> unsuback = {0xB0, 0x02, 0x00, 0x08};
    CHECK(mqtt_deserialize_unsuback(unsuback.data(), unsuback.size(), &id) == MQTT_OK);
    CHECK(id == 8);
    unsuback[1] = 0x05;
    CHECK(mqtt_deserialize_unsuback(unsuback.data(), unsuback.size(), &id) == MQTT_ERR_MALFORMED_PACKET);
}

TEST_CASE(regression_client_packet_id_never_zero) {
    mqtt_client_t c;
    std::vector<uint8_t> sent;
    mqtt_client_init(&c, [](const uint8_t *d, size_t n, void *u) {
        auto *v = static_cast<std::vector<uint8_t> *>(u);
        v->assign(d, d + n);
    }, &sent);
    c.next_packet_id = 0xFFFF;
    CHECK(mqtt_client_subscribe(&c, "a", MQTT_QOS_0));
    CHECK(c.next_packet_id == 1);
    CHECK(mqtt_client_subscribe(&c, "a", MQTT_QOS_0));
    uint16_t id = (uint16_t)((sent[2] << 8) | sent[3]);
    CHECK(id == 1);
}

EMBED_TEST_MAIN()
