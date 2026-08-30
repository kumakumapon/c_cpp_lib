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

#define UART_PROTO_MAX_PAYLOAD      512     /**< 最大ペイロード長 (バイト) */
#define UART_PROTO_HEADER_SIZE      4       /**< TYPE(1) + SEQ(1) + LEN(2) */
#define UART_PROTO_OVERHEAD_SIZE    7       /**< SOF(1) + HEADER(4) + CRC(2) + EOF(1) */
#define UART_PROTO_MAX_FRAME_SIZE   (UART_PROTO_OVERHEAD_SIZE + UART_PROTO_MAX_PAYLOAD)

/* パケット種別 (Packet Types) */
typedef enum {
    UART_PKT_TYPE_DATA     = 0x01,  /**< 通常データ転送 */
    UART_PKT_TYPE_ACK      = 0x02,  /**< 肯定応答 (ACK) */
    UART_PKT_TYPE_NACK     = 0x03,  /**< 否定応答 (NACK) */
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
    UART_PARSE_ERR_DROP    = -4     /**< 受信バッファ溢れ等による破棄 */
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
} uart_proto_stats_t;

/* コールバック型定義 */
typedef void (*uart_proto_tx_fn)(const uint8_t *data, size_t length, void *user_data);
typedef void (*uart_proto_rx_callback)(const uart_packet_t *packet, void *user_data);

/* プロトコルスタック管理構造体 */
typedef struct {
    uart_parser_state_t state;
    uart_packet_t rx_pkt;
    size_t payload_index;
    uint16_t calc_crc;
    uint8_t next_tx_seq;

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

/**
 * @brief プロトコルスタックを初期化します。
 */
void uart_proto_init(uart_protocol_t *proto, uart_proto_tx_fn tx_fn, uart_proto_rx_callback rx_cb, void *user_data);

/**
 * @brief パケットをフレームにエンコードします。
 * 
 * @param type パケット種別
 * @param seq シーケンス番号
 * @param payload ペイロードデータ
 * @param payload_len ペイロード長
 * @param out_frame エンコード結果を格納するバッファ (UART_PROTO_MAX_FRAME_SIZE 以上必要)
 * @param out_frame_len エンコード後の総フレーム長が格納されるポインタ
 * @return true 成功
 * @return false ペイロード長超過または引数不正
 */
bool uart_proto_encode(uint8_t type, uint8_t seq, const uint8_t *payload, uint16_t payload_len,
                       uint8_t *out_frame, size_t *out_frame_len);

/**
 * @brief パケットを送信します (登録されたtx_fnを使用)。
 */
bool uart_proto_send(uart_protocol_t *proto, uint8_t type, const uint8_t *payload, uint16_t payload_len);

/**
 * @brief ACKパケットを送信します。
 */
bool uart_proto_send_ack(uart_protocol_t *proto, uint8_t target_seq);

/**
 * @brief NACKパケットを送信します。
 */
bool uart_proto_send_nack(uart_protocol_t *proto, uint8_t target_seq);

/**
 * @brief PINGパケットを送信します。
 */
bool uart_proto_send_ping(uart_protocol_t *proto);

/**
 * @brief 1バイト受信データをステートマシンに供給します (UART ISR等から呼び出し)。
 * 
 * @return uart_parse_status_t パース結果
 */
uart_parse_status_t uart_proto_parse_byte(uart_protocol_t *proto, uint8_t byte);

/**
 * @brief 複数バイト受信データを一括で処理します。
 */
void uart_proto_process_bytes(uart_protocol_t *proto, const uint8_t *data, size_t length);

/**
 * @brief 統計情報を取得します。
 */
void uart_proto_get_stats(const uart_protocol_t *proto, uart_proto_stats_t *stats);

/**
 * @brief 統計情報をリセットします。
 */
void uart_proto_reset_stats(uart_protocol_t *proto);

#ifdef __cplusplus
}
#endif

#endif /* UART_PROTOCOL_H */
