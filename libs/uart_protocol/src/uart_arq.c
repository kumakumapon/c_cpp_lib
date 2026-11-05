#include "uart_protocol/uart_arq.h"
#include <string.h>

static bool is_control(uint8_t type) {
    return type == UART_PKT_TYPE_ACK || type == UART_PKT_TYPE_NACK || type == UART_PKT_TYPE_PING ||
           type == UART_PKT_TYPE_PONG;
}

bool uart_arq_init(uart_arq_t *arq, uart_protocol_t *proto, uint32_t timeout_ms,
                   uint8_t max_retries) {
    if (!arq || !proto || timeout_ms == 0)
        return false;
    memset(arq, 0, sizeof(*arq));
    arq->proto = proto;
    arq->timeout_ms = timeout_ms;
    arq->max_retries = max_retries;
    return true;
}

void uart_arq_set_callbacks(uart_arq_t *arq, uart_arq_receive_fn on_receive,
                            uart_arq_result_fn on_result, void *user) {
    if (!arq)
        return;
    arq->on_receive = on_receive;
    arq->on_result = on_result;
    arq->user = user;
}

static bool transmit(uart_arq_t *arq) {
    return uart_proto_send_seq(arq->proto, arq->tx_type, arq->tx_seq, arq->tx_payload,
                               arq->tx_len);
}

bool uart_arq_send(uart_arq_t *arq, uint8_t type, const uint8_t *payload, uint16_t len,
                   uint32_t now_ms, uint8_t *seq_out) {
    if (!arq || !arq->proto || arq->busy || is_control(type) || len > UART_PROTO_MAX_PAYLOAD ||
        (len && !payload))
        return false;
    arq->tx_type = type;
    arq->tx_seq = arq->next_seq;
    arq->tx_len = len;
    if (len)
        memcpy(arq->tx_payload, payload, len);
    if (!transmit(arq))
        return false;
    arq->next_seq++;
    arq->busy = true;
    arq->retries = 0;
    arq->sent_at_ms = now_ms;
    arq->now_ms = now_ms;
    arq->stats.tx_packets++;
    if (seq_out)
        *seq_out = arq->tx_seq;
    return true;
}

bool uart_arq_busy(const uart_arq_t *arq) { return arq && arq->busy; }

static void finish(uart_arq_t *arq, bool delivered) {
    arq->busy = false;
    if (delivered)
        arq->stats.delivered++;
    else
        arq->stats.failed++;
    if (arq->on_result)
        arq->on_result(arq->tx_seq, delivered, arq->user);
}

/* 再送する。上限に達していれば失敗として終える。 */
static void retry(uart_arq_t *arq, uint32_t now_ms) {
    if (arq->retries >= arq->max_retries) {
        finish(arq, false);
        return;
    }
    arq->retries++;
    arq->stats.retransmits++;
    arq->sent_at_ms = now_ms;
    (void)transmit(arq);
}

void uart_arq_on_packet(uart_arq_t *arq, const uart_packet_t *pkt) {
    if (!arq || !pkt)
        return;
    if (pkt->type == UART_PKT_TYPE_ACK || pkt->type == UART_PKT_TYPE_NACK) {
        /* 対象 SEQ が今の送信待ちと一致するときだけ扱う (古い ACK は無視) */
        if (!arq->busy || pkt->length != 1 || pkt->payload[0] != arq->tx_seq)
            return;
        if (pkt->type == UART_PKT_TYPE_ACK)
            finish(arq, true);
        else
            retry(arq, arq->now_ms);
        return;
    }
    if (is_control(pkt->type)) {
        /* PING/PONG は確認応答の対象外。そのままアプリへ */
        if (arq->on_receive)
            arq->on_receive(pkt, arq->user);
        return;
    }
    /* データ系: 必ず ACK を返す (重複でも返さないと相手が再送し続ける) */
    (void)uart_proto_send_ack(arq->proto, pkt->seq);
    if (arq->have_last_rx && pkt->seq == arq->last_rx_seq) {
        arq->stats.rx_duplicates++;
        return;
    }
    arq->have_last_rx = true;
    arq->last_rx_seq = pkt->seq;
    arq->stats.rx_delivered++;
    if (arq->on_receive)
        arq->on_receive(pkt, arq->user);
}

void uart_arq_proto_rx(const uart_packet_t *packet, void *arq) {
    uart_arq_on_packet((uart_arq_t *)arq, packet);
}

void uart_arq_poll(uart_arq_t *arq, uint32_t now_ms) {
    if (!arq)
        return;
    arq->now_ms = now_ms;
    if (arq->busy && (uint32_t)(now_ms - arq->sent_at_ms) >= arq->timeout_ms)
        retry(arq, now_ms);
}

void uart_arq_reset(uart_arq_t *arq) {
    if (!arq)
        return;
    arq->busy = false;
    arq->have_last_rx = false;
}
