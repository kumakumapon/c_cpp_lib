#include <iostream>
#include <vector>
#include <string>
#include <cstdio>

#include "embed_mqtt/mqtt_types.h"
#include "embed_mqtt/mqtt_packet.h"
#include "embed_mqtt/mqtt_topic.h"
#include "embed_mqtt/mqtt_client.h"
#include "embed_mqtt/embed_mqtt.hpp"

static void print_hex_packet(const char *label, const uint8_t *data, size_t len) {
    std::printf("  %s (%zu bytes): ", label, len);
    for (size_t i = 0; i < len; ++i) {
        std::printf("%02X ", data[i]);
    }
    std::printf("\n");
}

int main() {
    std::cout << "=====================================================" << std::endl;
    std::cout << " MQTT v3.1.1 Protocol Stack (embed_mqtt) Demo" << std::endl;
    std::cout << "=====================================================" << std::endl;

    std::vector<uint8_t> tcp_tx_pipe; // クライアント -> ブローカー
    std::vector<uint8_t> tcp_rx_pipe; // ブローカー -> クライアント

    embed_mqtt::Client client;

    // 1. 送信ハンドラ (TCPソケット送信相当)
    client.set_send_handler([&tcp_tx_pipe](const uint8_t *data, size_t len) {
        tcp_tx_pipe.insert(tcp_tx_pipe.end(), data, data + len);
    });

    // 2. 接続完了ハンドラ
    client.set_connect_handler([](mqtt_connack_rc_t rc, bool session_present) {
        std::printf("  [Client Event] CONNACK Received! Code: %d (Session Present: %s)\n",
                    rc, session_present ? "true" : "false");
    });

    // 3. メッセージ受信ハンドラ
    client.set_message_handler([](const std::string &topic, const std::vector<uint8_t> &payload, mqtt_qos_t qos) {
        std::string payload_str(payload.begin(), payload.end());
        std::printf("  [Client Event] Message Arrived! Topic: '%s' | Payload: '%s' | QoS: %d\n",
                    topic.c_str(), payload_str.c_str(), qos);
    });

    // ---------------------------------------------------------
    // ステップ 1: ブローカーへの接続 (CONNECT)
    // ---------------------------------------------------------
    std::cout << "\n>>> Step 1: Connect to MQTT Broker <<<\n";
    client.connect("iot_sensor_device_01", true, 60);
    print_hex_packet("[Client TX -> Network]", tcp_tx_pipe.data(), tcp_tx_pipe.size());
    tcp_tx_pipe.clear();

    // 仮想ブローカー: CONNACK を返信
    std::cout << "  [Broker] Returning CONNACK (Accepted)...\n";
    uint8_t connack_pkt[] = {0x20, 0x02, 0x00, 0x00};
    client.feed_bytes(connack_pkt, sizeof(connack_pkt));

    // ---------------------------------------------------------
    // ステップ 2: コマンドトピックの購読 (SUBSCRIBE)
    // ---------------------------------------------------------
    std::cout << "\n>>> Step 2: Subscribe to 'device/cmd/#' <<<\n";
    client.subscribe("device/cmd/#", MQTT_QOS_1);
    print_hex_packet("[Client TX -> Network]", tcp_tx_pipe.data(), tcp_tx_pipe.size());
    tcp_tx_pipe.clear();

    // 仮想ブローカー: SUBACK を返信
    std::cout << "  [Broker] Returning SUBACK (Packet ID: 1, Granted QoS: 1)...\n";
    uint8_t suback_pkt[] = {0x90, 0x03, 0x00, 0x01, 0x01};
    client.feed_bytes(suback_pkt, sizeof(suback_pkt));

    // ---------------------------------------------------------
    // ステップ 3: センサデータの定期発行 (PUBLISH)
    // ---------------------------------------------------------
    std::cout << "\n>>> Step 3: Publish Sensor Telemetry Data <<<\n";
    std::string telemetry_json = "{\"temp\": 24.6, \"humidity\": 58.2, \"battery\": 95}";
    client.publish("iot/sensors/env_data", telemetry_json, MQTT_QOS_0);
    print_hex_packet("[Client TX -> Network]", tcp_tx_pipe.data(), tcp_tx_pipe.size());
    tcp_tx_pipe.clear();

    // ---------------------------------------------------------
    // ステップ 4: クラウド側からの遠隔コマンド受信 (Incoming PUBLISH)
    // ---------------------------------------------------------
    std::cout << "\n>>> Step 4: Incoming Remote Command from Cloud <<<\n";
    mqtt_message_t cmd_msg{};
    cmd_msg.topic = "device/cmd/restart";
    cmd_msg.payload = (const uint8_t *)"REBOOT_NOW";
    cmd_msg.payload_len = 10;
    cmd_msg.qos = MQTT_QOS_1;
    cmd_msg.packet_id = 42;

    uint8_t incoming_pub_buf[128];
    size_t incoming_pub_len = 0;
    mqtt_serialize_publish(&cmd_msg, incoming_pub_buf, sizeof(incoming_pub_buf), &incoming_pub_len);

    std::cout << "  [Broker] Sending remote command to client...\n";
    client.feed_bytes(incoming_pub_buf, incoming_pub_len);

    // 自動返信された PUBACK を確認
    if (!tcp_tx_pipe.empty()) {
        print_hex_packet("[Client Auto-Response (PUBACK)]", tcp_tx_pipe.data(), tcp_tx_pipe.size());
        tcp_tx_pipe.clear();
    }

    // ---------------------------------------------------------
    // ステップ 5: 切断 (DISCONNECT)
    // ---------------------------------------------------------
    std::cout << "\n>>> Step 5: Disconnect from Broker <<<\n";
    client.disconnect();
    print_hex_packet("[Client TX -> Network]", tcp_tx_pipe.data(), tcp_tx_pipe.size());
    tcp_tx_pipe.clear();

    std::cout << "\n=====================================================" << std::endl;
    std::cout << " embed_mqtt demonstration completed successfully!" << std::endl;
    std::cout << "=====================================================" << std::endl;
    return 0;
}
