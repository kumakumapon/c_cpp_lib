#pragma once
#ifndef MQTT_TYPES_H
#define MQTT_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------
 * MQTT プロトコル定数
 * --------------------------------------------------------- */
#define MQTT_PROTOCOL_VERSION_3_1_1 4       /**< MQTT v3.1.1 プロトコルレベル */
#define MQTT_MAX_PACKET_SIZE        1024    /**< 最大パケットサイズ */
#define MQTT_DEFAULT_KEEP_ALIVE     60      /**< デフォルトKeep Alive秒数 */

/* ---------------------------------------------------------
 * コントロールパケットタイプ (Packet Types)
 * --------------------------------------------------------- */
typedef enum {
    MQTT_PACKET_TYPE_RESERVED    = 0,
    MQTT_PACKET_TYPE_CONNECT     = 1,   /**< クライアント接続要求 */
    MQTT_PACKET_TYPE_CONNACK     = 2,   /**< 接続確認応答 */
    MQTT_PACKET_TYPE_PUBLISH     = 3,   /**< メッセージ発行 */
    MQTT_PACKET_TYPE_PUBACK      = 4,   /**< QoS 1 発行確認 */
    MQTT_PACKET_TYPE_PUBREC      = 5,   /**< QoS 2 発行受信 */
    MQTT_PACKET_TYPE_PUBREL      = 6,   /**< QoS 2 発行解放 */
    MQTT_PACKET_TYPE_PUBCOMP     = 7,   /**< QoS 2 発行完了 */
    MQTT_PACKET_TYPE_SUBSCRIBE   = 8,   /**< 購読要求 */
    MQTT_PACKET_TYPE_SUBACK      = 9,   /**< 購読確認応答 */
    MQTT_PACKET_TYPE_UNSUBSCRIBE = 10,  /**< 購読解除要求 */
    MQTT_PACKET_TYPE_UNSUBACK    = 11,  /**< 購読解除確認応答 */
    MQTT_PACKET_TYPE_PINGREQ     = 12,  /**< PING要求 */
    MQTT_PACKET_TYPE_PINGRESP    = 13,  /**< PING応答 */
    MQTT_PACKET_TYPE_DISCONNECT  = 14   /**< 切断要求 */
} mqtt_packet_type_t;

/* ---------------------------------------------------------
 * QoS レベル (Quality of Service)
 * --------------------------------------------------------- */
typedef enum {
    MQTT_QOS_0 = 0, /**< At most once (最大1回 / 配信保証なし) */
    MQTT_QOS_1 = 1, /**< At least once (最低1回 / ACK確認あり) */
    MQTT_QOS_2 = 2, /**< Exactly once (正確に1回 / 4ウェイハンドシェイク) */
} mqtt_qos_t;

/* ---------------------------------------------------------
 * CONNACK 接続リターンコード
 * --------------------------------------------------------- */
typedef enum {
    MQTT_CONNACK_ACCEPTED                    = 0, /**< 接続成功 */
    MQTT_CONNACK_UNACCEPTABLE_PROTOCOL_VER   = 1, /**< 未サポートプロトコルバージョン */
    MQTT_CONNACK_IDENTIFIER_REJECTED         = 2, /**< クライアントID不正 */
    MQTT_CONNACK_SERVER_UNAVAILABLE          = 3, /**< サーバ利用不可 */
    MQTT_CONNACK_BAD_USER_OR_PASSWORD        = 4, /**< ユーザー名/パスワード不正 */
    MQTT_CONNACK_NOT_AUTHORIZED              = 5  /**< 認証失敗 (未許可) */
} mqtt_connack_rc_t;

/* ---------------------------------------------------------
 * 接続オプション構造体
 * --------------------------------------------------------- */
typedef struct {
    const char *client_id;      /**< クライアント識別子 (必須) */
    const char *username;       /**< ユーザー名 (NULL可) */
    const char *password;       /**< パスワード (NULL可) */
    bool clean_session;         /**< クリーンセッションフラグ */
    uint16_t keep_alive_sec;    /**< Keep Alive秒数 */

    /* 遺言 (Will) 設定 */
    const char *will_topic;     /**< 遺言トピック (NULL時は遺言なし) */
    const uint8_t *will_message;/**< 遺言メッセージペイロード */
    uint16_t will_message_len;  /**< 遺言メッセージ長 */
    mqtt_qos_t will_qos;        /**< 遺言QoS */
    bool will_retain;           /**< 遺言Retainフラグ */
} mqtt_connect_options_t;

/* ---------------------------------------------------------
 * メッセージ構造体
 * --------------------------------------------------------- */
typedef struct {
    const char *topic;          /**< トピック名 */
    uint16_t topic_len;         /**< トピック名長 */
    const uint8_t *payload;     /**< ペイロードデータ */
    size_t payload_len;         /**< ペイロード長 */
    mqtt_qos_t qos;             /**< QoSレベル */
    bool retain;                /**< Retainフラグ */
    bool dup;                   /**< DUPフラグ (再送フラグ) */
    uint16_t packet_id;         /**< パケット識別子 (QoS > 0 で有効) */
} mqtt_message_t;

/* ---------------------------------------------------------
 * エラーコード
 * --------------------------------------------------------- */
typedef enum {
    MQTT_OK                      = 0,
    MQTT_ERR_INVALID_PARAM       = -1,
    MQTT_ERR_BUFFER_OVERFLOW     = -2,
    MQTT_ERR_INCOMPLETE_PACKET   = -3,
    MQTT_ERR_MALFORMED_PACKET    = -4,
    MQTT_ERR_CONNECTION_REFUSED  = -5,
    MQTT_ERR_NOT_CONNECTED       = -6
} mqtt_status_t;

#ifdef __cplusplus
}
#endif

#endif /* MQTT_TYPES_H */
