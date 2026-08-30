#pragma once
#include "mqtt_types.h"
#include "mqtt_packet.h"
#include "mqtt_topic.h"
#include "mqtt_client.h"

#include <functional>
#include <string>
#include <vector>
#include <map>

namespace embed_mqtt {

class Client {
public:
    using SendHandler = std::function<void(const uint8_t *data, size_t len)>;
    using ConnectHandler = std::function<void(mqtt_connack_rc_t rc, bool session_present)>;
    using MessageHandler = std::function<void(const std::string &topic, const std::vector<uint8_t> &payload, mqtt_qos_t qos)>;

    Client() {
        mqtt_client_init(&client_, &c_send_adapter, this);
        mqtt_client_set_callbacks(&client_, &c_connect_adapter, &c_message_adapter, nullptr, nullptr);
    }

    explicit Client(SendHandler send_fn) : Client() {
        send_fn_ = std::move(send_fn);
    }

    void set_send_handler(SendHandler handler) {
        send_fn_ = std::move(handler);
    }

    void set_connect_handler(ConnectHandler handler) {
        connect_fn_ = std::move(handler);
    }

    void set_message_handler(MessageHandler handler) {
        message_fn_ = std::move(handler);
    }

    bool connect(const mqtt_connect_options_t &options) {
        return mqtt_client_connect(&client_, &options);
    }

    bool connect(const std::string &client_id, bool clean_session = true, uint16_t keep_alive = 60) {
        mqtt_connect_options_t opts{};
        opts.client_id = client_id.c_str();
        opts.clean_session = clean_session;
        opts.keep_alive_sec = keep_alive;
        return connect(opts);
    }

    bool publish(const std::string &topic, const std::string &message, mqtt_qos_t qos = MQTT_QOS_0, bool retain = false) {
        return mqtt_client_publish(&client_, topic.c_str(), message.data(), message.size(), qos, retain);
    }

    bool publish(const std::string &topic, const std::vector<uint8_t> &payload, mqtt_qos_t qos = MQTT_QOS_0, bool retain = false) {
        return mqtt_client_publish(&client_, topic.c_str(), payload.data(), payload.size(), qos, retain);
    }

    bool subscribe(const std::string &topic, mqtt_qos_t qos = MQTT_QOS_0) {
        return mqtt_client_subscribe(&client_, topic.c_str(), qos);
    }

    bool unsubscribe(const std::string &topic) {
        return mqtt_client_unsubscribe(&client_, topic.c_str());
    }

    bool ping() {
        return mqtt_client_ping(&client_);
    }

    bool disconnect() {
        return mqtt_client_disconnect(&client_);
    }

    void feed_bytes(const uint8_t *data, size_t len) {
        mqtt_client_feed_bytes(&client_, data, len);
    }

    void feed_bytes(const std::vector<uint8_t> &data) {
        feed_bytes(data.data(), data.size());
    }

    void yield(uint32_t current_time_sec) {
        mqtt_client_yield(&client_, current_time_sec);
    }

    bool is_connected() const {
        return client_.state == MQTT_STATE_CONNECTED;
    }

private:
    static void c_send_adapter(const uint8_t *data, size_t len, void *ud) {
        auto *self = static_cast<Client *>(ud);
        if (self && self->send_fn_) {
            self->send_fn_(data, len);
        }
    }

    static void c_connect_adapter(mqtt_client_t *, mqtt_connack_rc_t rc, bool session_present, void *ud) {
        auto *self = static_cast<Client *>(ud);
        if (self && self->connect_fn_) {
            self->connect_fn_(rc, session_present);
        }
    }

    static void c_message_adapter(mqtt_client_t *, const mqtt_message_t *msg, void *ud) {
        auto *self = static_cast<Client *>(ud);
        if (self && self->message_fn_ && msg) {
            std::string topic(msg->topic, msg->topic_len);
            std::vector<uint8_t> payload(msg->payload, msg->payload + msg->payload_len);
            self->message_fn_(topic, payload, msg->qos);
        }
    }

    mqtt_client_t client_{};
    SendHandler send_fn_;
    ConnectHandler connect_fn_;
    MessageHandler message_fn_;
};

} // namespace embed_mqtt
