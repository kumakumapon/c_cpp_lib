#pragma once
#include "uart_protocol.h"

#include <cstring>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace uart_protocol {

/**
 * uart_protocol_t の C++ ラッパー。
 * C 側のコールバックに this を渡しているため、コピー・ムーブはできない
 * (コピーすると古いオブジェクトのハンドラが呼ばれてしまう)。
 */
class Protocol {
  public:
    using TxHandler = std::function<void(const uint8_t *data, size_t length)>;
    using RxHandler = std::function<void(const uart_packet_t &packet)>;

    Protocol() { uart_proto_init(&proto_, &c_tx_adapter, &c_rx_adapter, this); }

    explicit Protocol(TxHandler tx_handler, RxHandler rx_handler = nullptr) : Protocol() {
        tx_handler_ = std::move(tx_handler);
        rx_handler_ = std::move(rx_handler);
    }

    Protocol(const Protocol &) = delete;
    Protocol &operator=(const Protocol &) = delete;
    Protocol(Protocol &&) = delete;
    Protocol &operator=(Protocol &&) = delete;

    void set_tx_handler(TxHandler handler) { tx_handler_ = std::move(handler); }
    void set_rx_handler(RxHandler handler) { rx_handler_ = std::move(handler); }

    /** ペイロードが UART_PROTO_MAX_PAYLOAD を超える場合は送らずに false を返す。 */
    bool send(uint8_t type, const uint8_t *payload, size_t length) {
        if (length > UART_PROTO_MAX_PAYLOAD)
            return false;
        return uart_proto_send(&proto_, type, payload, static_cast<uint16_t>(length));
    }

    bool send(uint8_t type, const std::vector<uint8_t> &data) {
        return send(type, data.data(), data.size());
    }

    bool send_string(uint8_t type, std::string_view str) {
        return send(type, reinterpret_cast<const uint8_t *>(str.data()), str.size());
    }

    bool send_ack(uint8_t target_seq) { return uart_proto_send_ack(&proto_, target_seq); }
    bool send_nack(uint8_t target_seq) { return uart_proto_send_nack(&proto_, target_seq); }
    bool send_ping() { return uart_proto_send_ping(&proto_); }
    void set_auto_pong(bool enabled) { uart_proto_set_auto_pong(&proto_, enabled); }

    uart_parse_status_t parse_byte(uint8_t byte) { return uart_proto_parse_byte(&proto_, byte); }

    size_t process_bytes(const uint8_t *data, size_t length) {
        return uart_proto_process_bytes(&proto_, data, length);
    }

    size_t process_bytes(const std::vector<uint8_t> &data) {
        return process_bytes(data.data(), data.size());
    }

    void set_rx_timeout(uint32_t timeout_ms) { uart_proto_set_rx_timeout(&proto_, timeout_ms); }
    bool poll(uint32_t now_ms) { return uart_proto_poll(&proto_, now_ms); }
    void reset_rx() { uart_proto_reset_rx(&proto_); }

    uart_proto_stats_t get_stats() const {
        uart_proto_stats_t s{};
        uart_proto_get_stats(&proto_, &s);
        return s;
    }

    void reset_stats() { uart_proto_reset_stats(&proto_); }

    uart_protocol_t *raw_handle() { return &proto_; }
    const uart_protocol_t *raw_handle() const { return &proto_; }

  private:
    static void c_tx_adapter(const uint8_t *data, size_t length, void *user_data) {
        auto *self = static_cast<Protocol *>(user_data);
        if (self && self->tx_handler_)
            self->tx_handler_(data, length);
    }

    static void c_rx_adapter(const uart_packet_t *packet, void *user_data) {
        auto *self = static_cast<Protocol *>(user_data);
        if (self && self->rx_handler_ && packet)
            self->rx_handler_(*packet);
    }

    uart_protocol_t proto_;
    TxHandler tx_handler_;
    RxHandler rx_handler_;
};

} // namespace uart_protocol
