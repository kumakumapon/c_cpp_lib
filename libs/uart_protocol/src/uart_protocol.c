#include "uart_protocol/uart_protocol.h"
#include <string.h>

void uart_proto_init(uart_protocol_t *proto, uart_proto_tx_fn tx_fn, uart_proto_rx_callback rx_cb,
                     void *user_data) {
    if (!proto)
        return;
    memset(proto, 0, sizeof(uart_protocol_t));
    proto->state = STATE_WAIT_SOF;
    proto->tx_fn = tx_fn;
    proto->rx_cb = rx_cb;
    proto->user_data = user_data;
}

bool uart_proto_encode(uint8_t type, uint8_t seq, const uint8_t *payload, uint16_t payload_len,
                       uint8_t *out_frame, size_t *out_frame_len) {
    if (!out_frame || !out_frame_len)
        return false;
    if (payload_len > UART_PROTO_MAX_PAYLOAD)
        return false;
    if (payload_len > 0 && !payload)
        return false;

    size_t idx = 0;
    out_frame[idx++] = UART_PROTO_SOF;
    out_frame[idx++] = type;
    out_frame[idx++] = seq;
    out_frame[idx++] = (uint8_t)((payload_len >> 8) & 0xFF);
    out_frame[idx++] = (uint8_t)(payload_len & 0xFF);

    if (payload_len > 0) {
        memcpy(&out_frame[idx], payload, payload_len);
        idx += payload_len;
    }

    /* CRC計算 (TYPEからPAYLOADの末尾まで) */
    uint16_t crc = uart_crc16_ccitt(&out_frame[1], UART_PROTO_HEADER_SIZE + (size_t)payload_len, 0xFFFF);
    out_frame[idx++] = (uint8_t)((crc >> 8) & 0xFF);
    out_frame[idx++] = (uint8_t)(crc & 0xFF);
    out_frame[idx++] = UART_PROTO_EOF;

    *out_frame_len = idx;
    return true;
}

bool uart_proto_send_seq(uart_protocol_t *proto, uint8_t type, uint8_t seq, const uint8_t *payload,
                         uint16_t payload_len) {
    if (!proto || !proto->tx_fn)
        return false;
    uint8_t frame_buf[UART_PROTO_MAX_FRAME_SIZE];
    size_t frame_len = 0;
    if (!uart_proto_encode(type, seq, payload, payload_len, frame_buf, &frame_len))
        return false;
    proto->tx_fn(frame_buf, frame_len, proto->user_data);
    proto->stats.tx_packets++;
    proto->stats.tx_bytes += (uint32_t)frame_len;
    return true;
}

bool uart_proto_send(uart_protocol_t *proto, uint8_t type, const uint8_t *payload,
                     uint16_t payload_len) {
    if (!proto || !proto->tx_fn)
        return false;
    /* 送信に失敗した場合は SEQ を消費しない */
    if (!uart_proto_send_seq(proto, type, proto->next_tx_seq, payload, payload_len))
        return false;
    proto->next_tx_seq++;
    return true;
}

bool uart_proto_send_ack(uart_protocol_t *proto, uint8_t target_seq) {
    return uart_proto_send(proto, UART_PKT_TYPE_ACK, &target_seq, 1);
}

bool uart_proto_send_nack(uart_protocol_t *proto, uint8_t target_seq) {
    return uart_proto_send(proto, UART_PKT_TYPE_NACK, &target_seq, 1);
}

bool uart_proto_send_ping(uart_protocol_t *proto) {
    return uart_proto_send(proto, UART_PKT_TYPE_PING, NULL, 0);
}

void uart_proto_set_auto_pong(uart_protocol_t *proto, bool enabled) {
    if (proto)
        proto->auto_pong = enabled;
}

/* ---------------------------------------------------------
 * 受信ステートマシン
 * --------------------------------------------------------- */

static uart_parse_status_t fail(uart_protocol_t *p, uart_parse_status_t status) {
    p->state = STATE_WAIT_SOF;
    /* 再走査中のエラーは「本物のフレームを探す試行」の副産物なので統計に数えない */
    if (!p->rescanning) {
        if (status == UART_PARSE_ERR_CRC)
            p->stats.rx_crc_errors++;
        else if (status == UART_PARSE_ERR_LEN)
            p->stats.rx_length_errors++;
        else
            p->stats.rx_format_errors++;
    }
    return status;
}

static void deliver(uart_protocol_t *p) {
    uart_packet_t *pkt = &p->rx_pkt;
    memcpy(pkt->payload, p->rx_raw + UART_PROTO_HEADER_SIZE, pkt->length);
    p->stats.rx_packets++;
    if (p->auto_pong && pkt->type == UART_PKT_TYPE_PING)
        (void)uart_proto_send_seq(p, UART_PKT_TYPE_PONG, pkt->seq, pkt->payload, pkt->length);
    if (p->rx_cb)
        p->rx_cb(pkt, p->user_data);
}

/* 1 バイト進める。SOF 以降のバイトは rx_raw に積む (エラー時の再走査用)。 */
static uart_parse_status_t step(uart_protocol_t *p, uint8_t byte) {
    if (p->state == STATE_WAIT_SOF) {
        if (byte == UART_PROTO_SOF) {
            p->state = STATE_READ_TYPE;
            p->calc_crc = 0xFFFF;
            p->payload_index = 0;
            p->rx_raw_len = 0;
        }
        return UART_PARSE_IN_PROGRESS;
    }

    p->rx_raw[p->rx_raw_len++] = byte;
    switch (p->state) {
    case STATE_READ_TYPE:
        p->rx_pkt.type = byte;
        p->calc_crc = uart_crc16_update(p->calc_crc, byte);
        p->state = STATE_READ_SEQ;
        return UART_PARSE_IN_PROGRESS;

    case STATE_READ_SEQ:
        p->rx_pkt.seq = byte;
        p->calc_crc = uart_crc16_update(p->calc_crc, byte);
        p->state = STATE_READ_LEN_H;
        return UART_PARSE_IN_PROGRESS;

    case STATE_READ_LEN_H:
        p->rx_pkt.length = (uint16_t)(byte << 8);
        p->calc_crc = uart_crc16_update(p->calc_crc, byte);
        p->state = STATE_READ_LEN_L;
        return UART_PARSE_IN_PROGRESS;

    case STATE_READ_LEN_L:
        p->rx_pkt.length |= (uint16_t)byte;
        p->calc_crc = uart_crc16_update(p->calc_crc, byte);
        if (p->rx_pkt.length > UART_PROTO_MAX_PAYLOAD)
            return fail(p, UART_PARSE_ERR_LEN);
        p->state = p->rx_pkt.length == 0 ? STATE_READ_CRC_H : STATE_READ_PAYLOAD;
        return UART_PARSE_IN_PROGRESS;

    case STATE_READ_PAYLOAD:
        p->calc_crc = uart_crc16_update(p->calc_crc, byte);
        if (++p->payload_index >= p->rx_pkt.length)
            p->state = STATE_READ_CRC_H;
        return UART_PARSE_IN_PROGRESS;

    case STATE_READ_CRC_H:
        p->rx_pkt.crc = (uint16_t)(byte << 8);
        p->state = STATE_READ_CRC_L;
        return UART_PARSE_IN_PROGRESS;

    case STATE_READ_CRC_L:
        p->rx_pkt.crc |= (uint16_t)byte;
        p->state = STATE_WAIT_EOF;
        return UART_PARSE_IN_PROGRESS;

    case STATE_WAIT_EOF:
        if (byte != UART_PROTO_EOF)
            return fail(p, UART_PARSE_ERR_FORMAT);
        if (p->rx_pkt.crc != p->calc_crc)
            return fail(p, UART_PARSE_ERR_CRC);
        p->state = STATE_WAIT_SOF;
        deliver(p);
        return UART_PARSE_OK;

    default:
        return fail(p, UART_PARSE_ERR_FORMAT);
    }
}

/*
 * 失敗したフレームの SOF の直後から、受信済みバイトをもう一度走査する。
 * 新しいフレームのバイトは rx_raw の先頭へ書き戻すが、書き込み位置は常に読み出し位置より
 * 手前なので、追加のバッファなしでその場で処理できる。再走査中に再び失敗した場合は、
 * 新しい SOF の直後からの分と未処理分をつなげて続ける。1 回の失敗で少なくとも SOF の
 * 1 バイトが減るので必ず終わる。
 */
static void rescan(uart_protocol_t *p) {
    size_t n = p->rx_raw_len, i = 0;
    p->rescanning = true;
    p->state = STATE_WAIT_SOF;
    p->rx_raw_len = 0;
    while (i < n) {
        const uint8_t byte = p->rx_raw[i++];
        if (step(p, byte) < 0) {
            const size_t keep = p->rx_raw_len;
            memmove(p->rx_raw + keep, p->rx_raw + i, n - i);
            n = keep + (n - i);
            i = 0;
            p->rx_raw_len = 0;
        }
    }
    p->rescanning = false;
}

uart_parse_status_t uart_proto_parse_byte(uart_protocol_t *proto, uint8_t byte) {
    if (!proto)
        return UART_PARSE_ERR_FORMAT;
    proto->stats.rx_bytes++;
    proto->rx_activity = true;
    const uart_parse_status_t r = step(proto, byte);
    if (r < 0)
        rescan(proto);
    return r;
}

size_t uart_proto_process_bytes(uart_protocol_t *proto, const uint8_t *data, size_t length) {
    if (!proto || !data)
        return 0;
    const uint32_t before = proto->stats.rx_packets;
    for (size_t i = 0; i < length; ++i)
        (void)uart_proto_parse_byte(proto, data[i]);
    return (size_t)(proto->stats.rx_packets - before);
}

void uart_proto_set_rx_timeout(uart_protocol_t *proto, uint32_t timeout_ms) {
    if (proto)
        proto->rx_timeout_ms = timeout_ms;
}

bool uart_proto_poll(uart_protocol_t *proto, uint32_t now_ms) {
    if (!proto)
        return false;
    if (proto->rx_activity) {
        proto->rx_activity = false;
        proto->last_activity_ms = now_ms;
        return false;
    }
    if (proto->rx_timeout_ms == 0 || proto->state == STATE_WAIT_SOF)
        return false;
    if ((uint32_t)(now_ms - proto->last_activity_ms) < proto->rx_timeout_ms)
        return false;
    proto->stats.rx_timeouts++;
    rescan(proto);
    /* 再走査の結果、また途中のフレームが残ったら、その時刻から改めて待つ */
    proto->last_activity_ms = now_ms;
    return true;
}

void uart_proto_reset_rx(uart_protocol_t *proto) {
    if (!proto)
        return;
    proto->state = STATE_WAIT_SOF;
    proto->rx_raw_len = 0;
    proto->payload_index = 0;
}

void uart_proto_get_stats(const uart_protocol_t *proto, uart_proto_stats_t *stats) {
    if (proto && stats)
        *stats = proto->stats;
}

void uart_proto_reset_stats(uart_protocol_t *proto) {
    if (proto)
        memset(&proto->stats, 0, sizeof(uart_proto_stats_t));
}
