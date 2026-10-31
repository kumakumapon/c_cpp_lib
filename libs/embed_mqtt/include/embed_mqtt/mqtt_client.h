#pragma once
#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H

#include "mqtt_types.h"
#include "mqtt_packet.h"
#include "mqtt_topic.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mqtt_client mqtt_client_t;

/* コールバック型定義 */
typedef void (*mqtt_send_fn)(const uint8_t *data, size_t len, void *user_data);
typedef void (*mqtt_on_connect_fn)(mqtt_client_t *client, mqtt_connack_rc_t rc, bool session_present, void *user_data);
typedef void (*mqtt_on_message_fn)(mqtt_client_t *client, const mqtt_message_t *msg, void *user_data);
typedef void (*mqtt_on_puback_fn)(mqtt_client_t *client, uint16_t packet_id, void *user_data);
typedef void (*mqtt_on_suback_fn)(mqtt_client_t *client, uint16_t packet_id, mqtt_qos_t granted_qos, void *user_data);

/* クライアント状態 */
typedef enum {
    MQTT_STATE_DISCONNECTED,
    MQTT_STATE_CONNECTING,
    MQTT_STATE_CONNECTED
} mqtt_client_state_t;

struct mqtt_client {
    mqtt_client_state_t state;
    uint16_t next_packet_id;
    uint16_t keep_alive_sec;
    uint32_t last_activity_time;
    bool tx_since_yield;        /**< 前回の yield 以降に何か送信したか (Keep Alive の起点) */

    /* 送信コールバック */
    mqtt_send_fn send_fn;

    /* イベントコールバック */
    mqtt_on_connect_fn on_connect;
    mqtt_on_message_fn on_message;
    mqtt_on_puback_fn  on_puback;
    mqtt_on_suback_fn  on_suback;

    void *user_data;

    /* 受信ストリームバッファ */
    uint8_t rx_buffer[MQTT_MAX_PACKET_SIZE];
    size_t rx_len;
};

/**
 * @brief MQTTクライアントを初期化します。
 */
void mqtt_client_init(mqtt_client_t *client, mqtt_send_fn send_fn, void *user_data);

/**
 * @brief イベントコールバックを設定します。
 */
void mqtt_client_set_callbacks(mqtt_client_t *client,
                               mqtt_on_connect_fn on_connect,
                               mqtt_on_message_fn on_message,
                               mqtt_on_puback_fn on_puback,
                               mqtt_on_suback_fn on_suback);

/**
 * @brief ブローカーへの接続リクエストを送信します。
 */
bool mqtt_client_connect(mqtt_client_t *client, const mqtt_connect_options_t *options);

/**
 * @brief メッセージを発行 (Publish) します。
 */
bool mqtt_client_publish(mqtt_client_t *client, const char *topic, const void *payload, size_t payload_len, mqtt_qos_t qos, bool retain);

/**
 * @brief トピックを購読 (Subscribe) します。
 */
bool mqtt_client_subscribe(mqtt_client_t *client, const char *topic, mqtt_qos_t qos);

/**
 * @brief トピックの購読を解除 (Unsubscribe) します。
 */
bool mqtt_client_unsubscribe(mqtt_client_t *client, const char *topic);

/**
 * @brief PING要求を送信します。
 */
bool mqtt_client_ping(mqtt_client_t *client);

/**
 * @brief 切断要求を送信します。
 */
bool mqtt_client_disconnect(mqtt_client_t *client);

/**
 * @brief 受信バイトストリームをクライアントに供給して処理します (ソケット/UART受信時に呼び出し)。
 */
void mqtt_client_feed_bytes(mqtt_client_t *client, const uint8_t *data, size_t len);

/**
 * @brief 定期的なタイマ処理を行います (KeepAliveの管理など)。
 * @param current_time_sec 現在時刻 (秒)
 */
void mqtt_client_yield(mqtt_client_t *client, uint32_t current_time_sec);

#ifdef __cplusplus
}
#endif

#endif /* MQTT_CLIENT_H */
