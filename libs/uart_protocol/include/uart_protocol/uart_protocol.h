#pragma once
#ifndef UART_PROTOCOL_H
#define UART_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "uart_crc.h"
#include "uart_ringbuf.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------
 * 定数定義
 * --------------------------------------------------------- */
#define UART_PROTO_SOF              0xAA    /**< フレーム開始バイト (Start of Frame) */
#define UART_PROTO_EOF              0x55    /**< フレーム終了バイト (End of Frame) */

/** 最大ペイロード長 (バイト)。RAM を節約したい場合は CMake の
 *  -DUART_PROTO_MAX_PAYLOAD=<n> で全体を同じ値に変更する (構造体サイズが変わるため)。 */
#ifndef UART_PROTO_MAX_PAYLOAD
#define UART_PROTO_MAX_PAYLOAD      512
#endif
#define UART_PROTO_HEADER_SIZE      4       /**< TYPE(1) + SEQ(1) + LEN(2) */
#define UART_PROTO_OVERHEAD_SIZE    8       /**< SOF(1) + HEADER(4) + CRC(2) + EOF(1) (旧版は 7 と誤記) */
#define UART_PROTO_MAX_FRAME_SIZE   (UART_PROTO_OVERHEAD_SIZE + UART_PROTO_MAX_PAYLOAD)

#if UART_PROTO_MAX_PAYLOAD < 1 || UART_PROTO_MAX_PAYLOAD > 65535
#error "UART_PROTO_MAX_PAYLOAD must be 1..65535"
#endif

/* パケット種別 (Packet Types) */
typedef enum {
    UART_PKT_TYPE_DATA     = 0x01,  /**< 通常データ転送 */
    UART_PKT_TYPE_ACK      = 0x02,  /**< 肯定応答 (ACK)。ペイロード 1 バイト = 対象 SEQ */
    UART_PKT_TYPE_NACK     = 0x03,  /**< 否定応答 (NACK)。ペイロード 1 バイト = 対象 SEQ */
    UART_PKT_TYPE_PING     = 0x04,  /**< 疎通確認 PING */
    UART_PKT_TYPE_PONG     = 0x05,  /**< 疎通応答 PONG */
    UART_PKT_TYPE_COMMAND  = 0x10,  /**< コマンド要求 */
    UART_PKT_TYPE_RESPONSE = 0x11,  /**< コマンド応答 */
    UART_PKT_TYPE_EVENT    = 0x20   /**< 非同期イベント通知 */
} uart_pkt_type_t;

/* パース結果ステータス */
typedef enum {
    UART_PARSE_IN_PROGRESS = 0,     /**< パケット受信・解析中 */
    UART_PARSE_OK          = 1,     /**< 正常パケット受信完了 */
    UART_PARSE_ERR_CRC     = -1,    /**< CRC不一致エラー */
    UART_PARSE_ERR_LEN     = -2,    /**< ペイロード長過大エラー */
    UART_PARSE_ERR_FORMAT  = -3,    /**< フレームフォーマット異常 (EOF不正等) */
    UART_PARSE_ERR_DROP    = -4     /**< 受信タイムアウトによる途中破棄 */
} uart_parse_status_t;

/* 受信パケット構造体 */
typedef struct {
    uint8_t  type;                          /**< パケット種別 */
    uint8_t  seq;                           /**< シーケンス番号 */
    uint16_t length;                        /**< ペイロード長 */
    uint8_t  payload[UART_PROTO_MAX_PAYLOAD]; /**< ペイロードデータ */
    uint16_t crc;                           /**< 受信したCRC16 */
} uart_packet_t;

/* パーサーステートマシン状態 */
typedef enum {
    STATE_WAIT_SOF = 0,
    STATE_READ_TYPE,
    STATE_READ_SEQ,
    STATE_READ_LEN_H,
    STATE_READ_LEN_L,
    STATE_READ_PAYLOAD,
    STATE_READ_CRC_H,
    STATE_READ_CRC_L,
    STATE_WAIT_EOF
} uart_parser_state_t;

/* 統計情報 */
typedef struct {
    uint32_t rx_bytes;              /**< 総受信バイト数 */
    uint32_t tx_bytes;              /**< 総送信バイト数 */
    uint32_t rx_packets;            /**< 正常受信パケット数 */
    uint32_t tx_packets;            /**< 正常送信パケット数 */
    uint32_t rx_crc_errors;         /**< CRCエラー数 */
    uint32_t rx_format_errors;      /**< フォーマット異常数 */
    uint32_t rx_length_errors;      /**< 長さ超過エラー数 */
    uint32_t rx_timeouts;           /**< 受信途中のタイムアウト数 */
} uart_proto_stats_t;

/* コールバック型定義 */
typedef void (*uart_proto_tx_fn)(const uint8_t *data, size_t length, void *user_data);
typedef void (*uart_proto_rx_callback)(const uart_packet_t *packet, void *user_data);

/* プロトコルスタック管理構造体 (メンバは内部用。API 経由で操作すること) */
typedef struct {
    uart_parser_state_t state;
    uart_packet_t rx_pkt;
    size_t payload_index;
    uint16_t calc_crc;
    uint8_t next_tx_seq;

    /* SOF 以降の生バイト。エラー時にここを再走査して、誤った SOF の後ろにある本物のフレームを拾う */
    uint8_t rx_raw[UART_PROTO_MAX_FRAME_SIZE];
    size_t rx_raw_len;
    bool rescanning;

    /* 受信タイムアウト */
    uint32_t rx_timeout_ms;
    uint32_t last_activity_ms;
    bool rx_activity;
    bool auto_pong;

    /* 送信・受信コールバック */
    uart_proto_tx_fn tx_fn;
    uart_proto_rx_callback rx_cb;
    void *user_data;

    /* 統計 */
    uart_proto_stats_t stats;
} uart_protocol_t;

/* ---------------------------------------------------------
 * API 関数宣言
 * --------------------------------------------------------- */

/** プロトコルスタックを初期化します。 */
void uart_proto_init(uart_protocol_t *proto, uart_proto_tx_fn tx_fn, uart_proto_rx_callback rx_cb, void *user_data);

/**
 * @brief パケットをフレームにエンコードします。
 * @param out_frame エンコード結果を格納するバッファ (UART_PROTO_OVERHEAD_SIZE + payload_len 以上)
 * @return false ペイロード長超過または引数不正
 */
bool uart_proto_encode(uint8_t type, uint8_t seq, const uint8_t *payload, uint16_t payload_len,
                       uint8_t *out_frame, size_t *out_frame_len);

/** パケットを送信します (SEQ は自動採番、登録された tx_fn を使用)。 */
bool uart_proto_send(uart_protocol_t *proto, uint8_t type, const uint8_t *payload, uint16_t payload_len);

/** SEQ を指定して送信します (再送など、同じ SEQ を使い回す場合)。自動採番の値は変えません。 */
bool uart_proto_send_seq(uart_protocol_t *proto, uint8_t type, uint8_t seq,
                         const uint8_t *payload, uint16_t payload_len);

/** ACK / NACK / PING を送信します。 */
bool uart_proto_send_ack(uart_protocol_t *proto, uint8_t target_seq);
bool uart_proto_send_nack(uart_protocol_t *proto, uint8_t target_seq);
bool uart_proto_send_ping(uart_protocol_t *proto);

/** 受信した PING に、同じ SEQ・同じペイロードの PONG を自動で返すかどうか (既定: 返さない)。
 *  PING は自動応答の有無にかかわらず rx_cb にも通知されます。 */
void uart_proto_set_auto_pong(uart_protocol_t *proto, bool enabled);

/**
 * @brief 1バイト受信データをステートマシンに供給します (UART ISR等から呼び出し)。
 *
 * エラーになった場合は、誤って SOF と判断したバイトの直後から受信済みバイトを再走査し、
 * その中にある正しいフレームを取りこぼさないようにします (見つかれば rx_cb を呼ぶ)。
 * 戻り値はこのバイト自体の判定結果です。
 */
uart_parse_status_t uart_proto_parse_byte(uart_protocol_t *proto, uint8_t byte);

/** 複数バイト受信データを一括で処理し、正常に受信したパケット数を返します。 */
size_t uart_proto_process_bytes(uart_protocol_t *proto, const uint8_t *data, size_t length);

/**
 * @brief 受信途中のフレームのタイムアウトを設定します (0 で無効、既定 0)。
 *
 * uart_proto_poll を定期的に呼ぶと、フレーム受信の途中で timeout_ms 以上バイトが
 * 来なかった場合に受信中のフレームを破棄し、受信済みバイトを再走査します。
 * ノイズで LEN が大きく読まれ、後続の正しいフレームを飲み込み続ける事態を防ぎます。
 */
void uart_proto_set_rx_timeout(uart_protocol_t *proto, uint32_t timeout_ms);

/**
 * @brief タイムアウト監視 (メインループから定期的に呼ぶ)。
 * @param now_ms 単調増加するミリ秒時刻 (32bit の周回は考慮済み)
 * @return true 受信途中のフレームをタイムアウトで破棄した
 */
bool uart_proto_poll(uart_protocol_t *proto, uint32_t now_ms);

/** 受信途中の状態を破棄して SOF 待ちに戻します (統計は変えない)。 */
void uart_proto_reset_rx(uart_protocol_t *proto);

/** 統計情報を取得 / リセットします。 */
void uart_proto_get_stats(const uart_protocol_t *proto, uart_proto_stats_t *stats);
void uart_proto_reset_stats(uart_protocol_t *proto);

#ifdef __cplusplus
}
#endif

#endif /* UART_PROTOCOL_H */
