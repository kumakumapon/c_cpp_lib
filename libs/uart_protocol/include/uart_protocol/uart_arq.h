#pragma once
#ifndef UART_ARQ_H
#define UART_ARQ_H

/*
 * uart_arq: uart_protocol の上に載せる Stop-and-Wait ARQ (確実な配送)。
 *
 * - uart_arq_send で送ったパケットは、相手の ACK (ペイロード = SEQ) が届くまで
 *   timeout_ms ごとに同じ SEQ で再送し、max_retries 回で諦めて on_result(false) を呼ぶ。
 * - NACK を受けたら待たずに再送する。
 * - 受信側は ACK/NACK/PING/PONG 以外のパケットに ACK を返し、直前と同じ SEQ の
 *   パケット (ACK が失われたことによる再送) はアプリへ渡さずに ACK だけ返す。
 * - 通信する両端がともに uart_arq を使うこと。SEQ は ARQ 専用の連番で、
 *   uart_proto_send の自動採番とは独立している。
 *
 * 使い方:
 *   uart_proto_init(&proto, uart_tx, uart_arq_proto_rx, &arq);   // rx は ARQ が受ける
 *   uart_arq_init(&arq, &proto, 50, 3);
 *   uart_arq_set_callbacks(&arq, on_receive, on_result, user);
 *   uart_arq_send(&arq, UART_PKT_TYPE_DATA, data, len, now_ms);
 *   // メインループ: uart_proto_process_bytes(...); uart_arq_poll(&arq, now_ms);
 */

#include "uart_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/** アプリ宛てのパケット (重複を除いたもの) */
typedef void (*uart_arq_receive_fn)(const uart_packet_t *packet, void *user);
/** 送信結果。delivered = true なら ACK 受信、false なら再送上限で失敗 */
typedef void (*uart_arq_result_fn)(uint8_t seq, bool delivered, void *user);

typedef struct {
    uint32_t tx_packets;    /**< uart_arq_send で受け付けた数 */
    uint32_t delivered;     /**< ACK を受けた数 */
    uint32_t failed;        /**< 再送上限で諦めた数 */
    uint32_t retransmits;   /**< 再送した回数 (タイムアウト + NACK) */
    uint32_t rx_delivered;  /**< アプリへ渡した受信パケット数 */
    uint32_t rx_duplicates; /**< 重複として破棄した受信パケット数 */
} uart_arq_stats_t;

typedef struct {
    uart_protocol_t *proto;
    uint32_t timeout_ms;
    uint8_t max_retries;

    /* 送信側 (未確認のパケットは 1 つだけ) */
    bool busy;
    uint8_t next_seq;
    uint8_t tx_type;
    uint8_t tx_seq;
    uint16_t tx_len;
    uint8_t tx_payload[UART_PROTO_MAX_PAYLOAD];
    uint32_t sent_at_ms;
    uint8_t retries;
    uint32_t now_ms;

    /* 受信側 */
    bool have_last_rx;
    uint8_t last_rx_seq;

    uart_arq_receive_fn on_receive;
    uart_arq_result_fn on_result;
    void *user;
    uart_arq_stats_t stats;
} uart_arq_t;

/** 初期化。timeout_ms は 1 以上、max_retries は再送回数 (0 なら再送しない)。 */
bool uart_arq_init(uart_arq_t *arq, uart_protocol_t *proto, uint32_t timeout_ms,
                   uint8_t max_retries);
void uart_arq_set_callbacks(uart_arq_t *arq, uart_arq_receive_fn on_receive,
                            uart_arq_result_fn on_result, void *user);

/**
 * @brief 確実な配送でパケットを送る。前のパケットが未確認 (busy) なら false。
 * @param type ACK/NACK/PING/PONG 以外の種別
 * @param seq_out 割り当てた SEQ (不要なら NULL)
 */
bool uart_arq_send(uart_arq_t *arq, uint8_t type, const uint8_t *payload, uint16_t len,
                   uint32_t now_ms, uint8_t *seq_out);
bool uart_arq_busy(const uart_arq_t *arq);

/** 受信パケットを ARQ に渡す。uart_protocol の rx_cb から呼ぶ。 */
void uart_arq_on_packet(uart_arq_t *arq, const uart_packet_t *packet);
/** uart_proto_init の rx_cb にそのまま渡せるアダプタ (user_data に uart_arq_t* を渡す)。 */
void uart_arq_proto_rx(const uart_packet_t *packet, void *arq);

/** 再送タイマの監視。メインループから定期的に呼ぶ。 */
void uart_arq_poll(uart_arq_t *arq, uint32_t now_ms);

/** 送信待ちを取り消す (on_result は呼ばない)。受信側の重複判定もリセットする。 */
void uart_arq_reset(uart_arq_t *arq);

#ifdef __cplusplus
}
#endif

#endif /* UART_ARQ_H */
