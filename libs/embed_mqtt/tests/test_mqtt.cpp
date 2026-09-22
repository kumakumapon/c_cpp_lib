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

EMBED_TEST_MAIN()
