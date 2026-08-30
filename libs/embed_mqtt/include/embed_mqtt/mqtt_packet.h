#pragma once
#ifndef MQTT_PACKET_H
#define MQTT_PACKET_H

#include "mqtt_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------
 * 可変長残り長エンコード/デコード
 * --------------------------------------------------------- */
size_t mqtt_encode_remaining_length(size_t length, uint8_t *dest);
size_t mqtt_decode_remaining_length(const uint8_t *src, size_t max_len, size_t *value);

/* ---------------------------------------------------------
 * パケットシリアライズ (エンコード) 関数群
 * --------------------------------------------------------- */

/**
 * @brief CONNECT パケットをエンコードします。
 */
bool mqtt_serialize_connect(const mqtt_connect_options_t *options, uint8_t *buf, size_t buf_size, size_t *out_len);

/**
 * @brief PUBLISH パケットをエンコードします。
 */
bool mqtt_serialize_publish(const mqtt_message_t *msg, uint8_t *buf, size_t buf_size, size_t *out_len);

/**
 * @brief PUBACK パケットをエンコードします (QoS 1応答)。
 */
bool mqtt_serialize_puback(uint16_t packet_id, uint8_t *buf, size_t buf_size, size_t *out_len);

/**
 * @brief SUBSCRIBE パケットをエンコードします。
 */
bool mqtt_serialize_subscribe(uint16_t packet_id, const char *topic, mqtt_qos_t qos, uint8_t *buf, size_t buf_size, size_t *out_len);

/**
 * @brief UNSUBSCRIBE パケットをエンコードします。
 */
bool mqtt_serialize_unsubscribe(uint16_t packet_id, const char *topic, uint8_t *buf, size_t buf_size, size_t *out_len);

/**
 * @brief PINGREQ パケットをエンコードします。
 */
bool mqtt_serialize_pingreq(uint8_t *buf, size_t buf_size, size_t *out_len);

/**
 * @brief DISCONNECT パケットをエンコードします。
 */
bool mqtt_serialize_disconnect(uint8_t *buf, size_t buf_size, size_t *out_len);

/* ---------------------------------------------------------
 * パケットデシリアライズ (デコード) 関数群
 * --------------------------------------------------------- */

/**
 * @brief バッファ先頭のパケットヘッダからパケット種別と総パケット長を判定します。
 */
mqtt_status_t mqtt_peek_packet_info(const uint8_t *buf, size_t len,
                                    mqtt_packet_type_t *type, size_t *total_packet_len);

/**
 * @brief CONNACK パケットをデコードします。
 */
mqtt_status_t mqtt_deserialize_connack(const uint8_t *buf, size_t len, bool *session_present, mqtt_connack_rc_t *rc);

/**
 * @brief PUBLISH パケットをデコードします。
 */
mqtt_status_t mqtt_deserialize_publish(const uint8_t *buf, size_t len, mqtt_message_t *msg);

/**
 * @brief PUBACK パケットをデコードします。
 */
mqtt_status_t mqtt_deserialize_puback(const uint8_t *buf, size_t len, uint16_t *packet_id);

/**
 * @brief SUBACK パケットをデコードします。
 */
mqtt_status_t mqtt_deserialize_suback(const uint8_t *buf, size_t len, uint16_t *packet_id, mqtt_qos_t *granted_qos);

/**
 * @brief UNSUBACK パケットをデコードします。
 */
mqtt_status_t mqtt_deserialize_unsuback(const uint8_t *buf, size_t len, uint16_t *packet_id);

#ifdef __cplusplus
}
#endif

#endif /* MQTT_PACKET_H */
