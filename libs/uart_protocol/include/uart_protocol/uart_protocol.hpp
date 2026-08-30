#pragma once
#include "uart_protocol.h"

#include <functional>
#include <vector>
#include <string>
#include <cstring>

namespace uart_protocol {

class Protocol {
public:
    using TxHandler = std::function<void(const uint8_t *data, size_t length)>;
    using RxHandler = std::function<void(const uart_packet_t &packet)>;

    Protocol() {
        uart_proto_init(&proto_, &c_tx_adapter, &c_rx_adapter, this);
    }

    explicit Protocol(TxHandler tx_handler, RxHandler rx_handler = nullptr) : Protocol() {
        tx_handler_ = std::move(tx_handler);
        rx_handler_ = std::move(rx_handler);
    }

    void set_tx_handler(TxHandler handler) {
        tx_handler_ = std::move(handler);
    }

    void set_rx_handler(RxHandler handler) {
        rx_handler_ = std::move(handler);
    }

    bool send(uint8_t type, const uint8_t *payload, uint16_t length) {
        return uart_proto_send(&proto_, type, payload, length);
    }

    bool send(uint8_t type, const std::vector<uint8_t> &data) {
        return send(type, data.data(), static_cast<uint16_t>(data.size()));
    }

    bool send_string(uint8_t type, const std::string &str) {
        return send(type, reinterpret_cast<const uint8_t *>(str.data()), static_cast<uint16_t>(str.size()));
    }

    bool send_ack(uint8_t target_seq) {
        return uart_proto_send_ack(&proto_, target_seq);
    }

    bool send_nack(uint8_t target_seq) {
        return uart_proto_send_nack(&proto_, target_seq);
    }

    bool send_ping() {
        return uart_proto_send_ping(&proto_, target_seq_dummy());
    }

    uart_parse_status_t parse_byte(uint8_t byte) {
        return uart_proto_parse_byte(&proto_, byte);
    }

    void process_bytes(const uint8_t *data, size_t length) {
        uart_proto_process_bytes(&proto_, data, length);
    }

    void process_bytes(const std::vector<uint8_t> &data) {
        process_bytes(data.data(), data.size());
    }

    uart_proto_stats_t get_stats() const {
        uart_proto_stats_t s{};
        uart_proto_get_stats(&proto_, &s);
        return s;
    }

    void reset_stats() {
        uart_proto_reset_stats(&proto_);
    }

    uart_protocol_t *raw_handle() { return &proto_; }
    const uart_protocol_t *raw_handle() const { return &proto_; }

private:
    static void c_tx_adapter(const uint8_t *data, size_t length, void *user_data) {
        auto *self = static_cast<Protocol *>(user_data);
        if (self && self->tx_handler_) {
            self->tx_handler_(data, length);
        }
    }

    static void c_rx_adapter(const uart_packet_t *packet, void *user_data) {
        auto *self = static_cast<Protocol *>(user_data);
        if (self && self->rx_handler_ && packet) {
            self->rx_handler_(*packet);
        }
    }

    static uint8_t target_seq_dummy() { return 0; }

    uart_protocol_t proto_;
    TxHandler tx_handler_;
    RxHandler rx_handler_;
};

} // namespace uart_protocol
