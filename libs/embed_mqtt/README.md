# embed_mqtt (組み込み向けMQTT v3.1.1プロトコルスタック)

`embed_mqtt` は、IoTデバイスや組み込みマイコン向けに設計された、ゼロ動的確保・軽量・ポータブルな **MQTT v3.1.1** クライアントプロトコルスタックライブラリです。

---

## 📑 目次
1. [特徴とメリット](#-特徴とメリット)
2. [サポートしているパケット & 機能](#-サポートしているパケット--機能)
3. [トピックワイルドカード](#-トピックワイルドカード)
4. [C API リファレンス](#-c-api-リファレンス)
5. [C++ API リファレンス](#-c-api-リファレンス-1)
6. [実装例 (Wi-Fi/Ethernet ソケット連携)](#-実装例-wifi-ethernet-ソケット連携)

---

## 🎯 特徴とメリット

- **MQTT v3.1.1 完全準拠**: CONNECT, CONNACK, PUBLISH, PUBACK, SUBSCRIBE, SUBACK, UNSUBSCRIBE, UNSUBACK, PINGREQ, PINGRESP, DISCONNECT を完全サポート。
- **ゼロ動的メモリ確保**: `malloc` や `free` を一切使用せず、固定長バッファとスタック領域でパケットのシリアライズ・デシリアライズを実施。
- **ハードウェア/ソケット抽象化**: 送信コールバック (`send_fn`) とバイト供給 (`feed_bytes`) によるストリーム駆動型設計により、lwIP、FreeRTOS+TCP、セルラーモジュール (ATコマンド)、UART等あらゆる伝送路に即座に対応。
- **トピックマッチング内蔵**: 単一レベルワイルドカード `+` および複数レベルワイルドカード `#` を高速判定。
- **QoS 0 & QoS 1 サポート**: QoS 1 メッセージ受信時の自動 `PUBACK` 送信に対応。

---

## 📦 サポートしているパケット & 機能

| パケット | 方向 | 説明 |
| :--- | :--- | :--- |
| `CONNECT` | Client $\rightarrow$ Broker | クライアント接続要求 (Clean Session, KeepAlive, 認証, 遺言) |
| `CONNACK` | Broker $\rightarrow$ Client | 接続応答 (Return Code, Session Present) |
| `PUBLISH` | 双方向 | メッセージ発行 (QoS 0, QoS 1, Retain, DUP) |
| `PUBACK` | 双方向 | QoS 1 発行受領応答 (Packet ID 確認) |
| `SUBSCRIBE` | Client $\rightarrow$ Broker | トピック購読要求 |
| `SUBACK` | Broker $\rightarrow$ Client | 購読確認応答 (Granted QoS) |
| `UNSUBSCRIBE` | Client $\rightarrow$ Broker | トピック購読解除要求 |
| `UNSUBACK` | Broker $\rightarrow$ Client | 購読解除確認応答 |
| `PINGREQ` | Client $\rightarrow$ Broker | KeepAlive 疎通確認要求 |
| `PINGRESP` | Broker $\rightarrow$ Client | KeepAlive 疎通確認応答 |
| `DISCONNECT` | Client $\rightarrow$ Broker | 正常切断要求 |

---

## 🔀 トピックワイルドカード

| パターン | 例 | マッチ対象 |
| :--- | :--- | :--- |
| `+` (単一レベル) | `sensor/+/temp` | `sensor/kitchen/temp`, `sensor/living/temp` |
| `#` (複数レベル) | `device/#` | `device/status`, `device/room1/light/level` |

---

## 📖 C API リファレンス

```c
#include "embed_mqtt/mqtt_client.h"

static void socket_send(const uint8_t *data, size_t len, void *user_data) {
    send(sock, data, len, 0);
}

static void on_message(mqtt_client_t *c, const mqtt_message_t *msg, void *ud) {
    printf("Topic: %.*s, Payload: %.*s\n", msg->topic_len, msg->topic, (int)msg->payload_len, msg->payload);
}

void app_main(void) {
    mqtt_client_t client;
    mqtt_client_init(&client, socket_send, NULL);
    mqtt_client_set_callbacks(&client, NULL, on_message, NULL, NULL);

    mqtt_connect_options_t opts = {
        .client_id = "stm32_node_01",
        .clean_session = true,
        .keep_alive_sec = 60
    };
    mqtt_client_connect(&client, &opts);

    // 受信ループ
    uint8_t rx_buf[256];
    int len = recv(sock, rx_buf, sizeof(rx_buf), 0);
    if (len > 0) {
        mqtt_client_feed_bytes(&client, rx_buf, len);
    }
}
```

---

## ⚡ C++ API リファレンス

```cpp
#include "embed_mqtt/embed_mqtt.hpp"

embed_mqtt::Client client;

client.set_send_handler([](const uint8_t *data, size_t len) {
    // ネットワークソケット送出
});

client.set_message_handler([](const std::string &topic, const std::vector<uint8_t> &payload, mqtt_qos_t qos) {
    // メッセージ到着処理
});

client.connect("iot_device_01");
client.subscribe("sensor/+/data");
client.publish("sensor/living/data", "{\"temp\": 24.5}");
```
