#include "uart_protocol/uart_protocol.h"
#include <string.h>

void uart_proto_init(uart_protocol_t *proto, uart_proto_tx_fn tx_fn, uart_proto_rx_callback rx_cb, void *user_data) {
    if (!proto) return;
    memset(proto, 0, sizeof(uart_protocol_t));
    proto->state = STATE_WAIT_SOF;
    proto->tx_fn = tx_fn;
    proto->rx_cb = rx_cb;
    proto->user_data = user_data;
    proto->next_tx_seq = 0;
}

bool uart_proto_encode(uint8_t type, uint8_t seq, const uint8_t *payload, uint16_t payload_len,
                       uint8_t *out_frame, size_t *out_frame_len) {
    if (!out_frame || !out_frame_len) return false;
    if (payload_len > UART_PROTO_MAX_PAYLOAD) return false;
    if (payload_len > 0 && !payload) return false;

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
    uint16_t crc = uart_crc16_ccitt(&out_frame[1], UART_PROTO_HEADER_SIZE + payload_len, 0xFFFF);
    out_frame[idx++] = (uint8_t)((crc >> 8) & 0xFF);
    out_frame[idx++] = (uint8_t)(crc & 0xFF);
    out_frame[idx++] = UART_PROTO_EOF;

    *out_frame_len = idx;
    return true;
}

bool uart_proto_send(uart_protocol_t *proto, uint8_t type, const uint8_t *payload, uint16_t payload_len) {
    if (!proto || !proto->tx_fn) return false;

    uint8_t frame_buf[UART_PROTO_MAX_FRAME_SIZE];
    size_t frame_len = 0;

    uint8_t seq = proto->next_tx_seq++;
    if (!uart_proto_encode(type, seq, payload, payload_len, frame_buf, &frame_len)) {
        return false;
    }

    proto->tx_fn(frame_buf, frame_len, proto->user_data);
    proto->stats.tx_packets++;
    proto->stats.tx_bytes += (uint32_t)frame_len;
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

uart_parse_status_t uart_proto_parse_byte(uart_protocol_t *proto, uint8_t byte) {
    if (!proto) return UART_PARSE_ERR_FORMAT;

    proto->stats.rx_bytes++;

    switch (proto->state) {
    case STATE_WAIT_SOF:
        if (byte == UART_PROTO_SOF) {
            proto->state = STATE_READ_TYPE;
            proto->calc_crc = 0xFFFF;
            proto->payload_index = 0;
        }
        return UART_PARSE_IN_PROGRESS;

    case STATE_READ_TYPE:
        proto->rx_pkt.type = byte;
        proto->calc_crc = uart_crc16_update(proto->calc_crc, byte);
        proto->state = STATE_READ_SEQ;
        return UART_PARSE_IN_PROGRESS;

    case STATE_READ_SEQ:
        proto->rx_pkt.seq = byte;
        proto->calc_crc = uart_crc16_update(proto->calc_crc, byte);
        proto->state = STATE_READ_LEN_H;
        return UART_PARSE_IN_PROGRESS;

    case STATE_READ_LEN_H:
        proto->rx_pkt.length = (uint16_t)(byte << 8);
        proto->calc_crc = uart_crc16_update(proto->calc_crc, byte);
        proto->state = STATE_READ_LEN_L;
        return UART_PARSE_IN_PROGRESS;

    case STATE_READ_LEN_L:
        proto->rx_pkt.length |= (uint16_t)byte;
        proto->calc_crc = uart_crc16_update(proto->calc_crc, byte);

        if (proto->rx_pkt.length > UART_PROTO_MAX_PAYLOAD) {
            proto->stats.rx_length_errors++;
            proto->state = STATE_WAIT_SOF;
            return UART_PARSE_ERR_LEN;
        }

        if (proto->rx_pkt.length == 0) {
            proto->state = STATE_READ_CRC_H;
        } else {
            proto->payload_index = 0;
            proto->state = STATE_READ_PAYLOAD;
        }
        return UART_PARSE_IN_PROGRESS;

    case STATE_READ_PAYLOAD:
        proto->rx_pkt.payload[proto->payload_index++] = byte;
        proto->calc_crc = uart_crc16_update(proto->calc_crc, byte);

        if (proto->payload_index >= proto->rx_pkt.length) {
            proto->state = STATE_READ_CRC_H;
        }
        return UART_PARSE_IN_PROGRESS;

    case STATE_READ_CRC_H:
        proto->rx_pkt.crc = (uint16_t)(byte << 8);
        proto->state = STATE_READ_CRC_L;
        return UART_PARSE_IN_PROGRESS;

    case STATE_READ_CRC_L:
        proto->rx_pkt.crc |= (uint16_t)byte;
        proto->state = STATE_WAIT_EOF;
        return UART_PARSE_IN_PROGRESS;

    case STATE_WAIT_EOF:
        proto->state = STATE_WAIT_SOF;
        if (byte != UART_PROTO_EOF) {
            proto->stats.rx_format_errors++;
            return UART_PARSE_ERR_FORMAT;
        }

        /* CRC検証 */
        if (proto->rx_pkt.crc != proto->calc_crc) {
            proto->stats.rx_crc_errors++;
            return UART_PARSE_ERR_CRC;
        }

        proto->stats.rx_packets++;
        if (proto->rx_cb) {
            proto->rx_cb(&proto->rx_pkt, proto->user_data);
        }
        return UART_PARSE_OK;

    default:
        proto->state = STATE_WAIT_SOF;
        return UART_PARSE_ERR_FORMAT;
    }
}

void uart_proto_process_bytes(uart_protocol_t *proto, const uint8_t *data, size_t length) {
    if (!proto || !data) return;
    for (size_t i = 0; i < length; ++i) {
        uart_proto_parse_byte(proto, data[i]);
    }
}

void uart_proto_get_stats(const uart_protocol_t *proto, uart_proto_stats_t *stats) {
    if (proto && stats) {
        *stats = proto->stats;
    }
}

void uart_proto_reset_stats(uart_protocol_t *proto) {
    if (proto) {
        memset(&proto->stats, 0, sizeof(uart_proto_stats_t));
    }
}
