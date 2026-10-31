#include "embed_mqtt/mqtt_client.h"
#include <string.h>

void mqtt_client_init(mqtt_client_t *client, mqtt_send_fn send_fn, void *user_data) {
    if (!client) return;
    memset(client, 0, sizeof(mqtt_client_t));
    client->state = MQTT_STATE_DISCONNECTED;
    client->send_fn = send_fn;
    client->user_data = user_data;
    client->next_packet_id = 1;
    client->keep_alive_sec = MQTT_DEFAULT_KEEP_ALIVE;
}

/* 送信はすべてここを通し、Keep Alive の起点となる「最後に送った時刻」を記録できるようにする */
static void client_send(mqtt_client_t *client, const uint8_t *data, size_t len) {
    client->tx_since_yield = true;
    client->send_fn(data, len, client->user_data);
}

/* Packet Identifier 0 は不正値のため、ラップアラウンド時に飛ばす */
static uint16_t next_packet_id(mqtt_client_t *client) {
    uint16_t id = client->next_packet_id++;
    if (client->next_packet_id == 0) client->next_packet_id = 1;
    return id ? id : next_packet_id(client);
}

void mqtt_client_set_callbacks(mqtt_client_t *client,
                               mqtt_on_connect_fn on_connect,
                               mqtt_on_message_fn on_message,
                               mqtt_on_puback_fn on_puback,
                               mqtt_on_suback_fn on_suback) {
    if (!client) return;
    client->on_connect = on_connect;
    client->on_message = on_message;
    client->on_puback  = on_puback;
    client->on_suback  = on_suback;
}

bool mqtt_client_connect(mqtt_client_t *client, const mqtt_connect_options_t *options) {
    if (!client || !options || !client->send_fn) return false;
    /* 2 つ目の CONNECT は不正なので、切断状態からのみ接続できる [MQTT-3.1.0-2] */
    if (client->state != MQTT_STATE_DISCONNECTED) return false;

    uint8_t tx_buf[MQTT_MAX_PACKET_SIZE];
    size_t tx_len = 0;

    if (!mqtt_serialize_connect(options, tx_buf, sizeof(tx_buf), &tx_len)) {
        return false;
    }

    client->keep_alive_sec = options->keep_alive_sec ? options->keep_alive_sec : MQTT_DEFAULT_KEEP_ALIVE;
    client->state = MQTT_STATE_CONNECTING;
    client_send(client, tx_buf, tx_len);
    return true;
}

bool mqtt_client_publish(mqtt_client_t *client, const char *topic, const void *payload, size_t payload_len, mqtt_qos_t qos, bool retain) {
    if (!client || !topic || !client->send_fn) return false;
    if (client->state == MQTT_STATE_DISCONNECTED) return false;

    mqtt_message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.topic = topic;
    msg.topic_len = (uint16_t)strlen(topic);
    msg.payload = (const uint8_t *)payload;
    msg.payload_len = payload_len;
    msg.qos = qos;
    msg.retain = retain;
    msg.dup = false;
    msg.packet_id = (qos > MQTT_QOS_0) ? next_packet_id(client) : 0;

    uint8_t tx_buf[MQTT_MAX_PACKET_SIZE];
    size_t tx_len = 0;

    if (!mqtt_serialize_publish(&msg, tx_buf, sizeof(tx_buf), &tx_len)) {
        return false;
    }

    client_send(client, tx_buf, tx_len);
    return true;
}

bool mqtt_client_subscribe(mqtt_client_t *client, const char *topic, mqtt_qos_t qos) {
    if (!client || !topic || !client->send_fn) return false;
    if (client->state == MQTT_STATE_DISCONNECTED) return false;

    uint16_t packet_id = next_packet_id(client);
    uint8_t tx_buf[MQTT_MAX_PACKET_SIZE];
    size_t tx_len = 0;

    if (!mqtt_serialize_subscribe(packet_id, topic, qos, tx_buf, sizeof(tx_buf), &tx_len)) {
        return false;
    }

    client_send(client, tx_buf, tx_len);
    return true;
}

bool mqtt_client_unsubscribe(mqtt_client_t *client, const char *topic) {
    if (!client || !topic || !client->send_fn) return false;
    if (client->state == MQTT_STATE_DISCONNECTED) return false;

    uint16_t packet_id = next_packet_id(client);
    uint8_t tx_buf[MQTT_MAX_PACKET_SIZE];
    size_t tx_len = 0;

    if (!mqtt_serialize_unsubscribe(packet_id, topic, tx_buf, sizeof(tx_buf), &tx_len)) {
        return false;
    }

    client_send(client, tx_buf, tx_len);
    return true;
}

bool mqtt_client_ping(mqtt_client_t *client) {
    if (!client || !client->send_fn) return false;
    if (client->state == MQTT_STATE_DISCONNECTED) return false;

    uint8_t tx_buf[4];
    size_t tx_len = 0;

    if (!mqtt_serialize_pingreq(tx_buf, sizeof(tx_buf), &tx_len)) {
        return false;
    }

    client_send(client, tx_buf, tx_len);
    return true;
}

bool mqtt_client_disconnect(mqtt_client_t *client) {
    if (!client || !client->send_fn) return false;
    if (client->state == MQTT_STATE_DISCONNECTED) return false;

    uint8_t tx_buf[4];
    size_t tx_len = 0;

    if (!mqtt_serialize_disconnect(tx_buf, sizeof(tx_buf), &tx_len)) {
        return false;
    }

    client_send(client, tx_buf, tx_len);
    client->state = MQTT_STATE_DISCONNECTED;
    return true;
}

static void process_single_packet(mqtt_client_t *client, const uint8_t *packet, size_t packet_len) {
    mqtt_packet_type_t ptype = (mqtt_packet_type_t)(packet[0] >> 4);

    switch (ptype) {
    case MQTT_PACKET_TYPE_CONNACK: {
        bool session_present = false;
        mqtt_connack_rc_t rc = MQTT_CONNACK_SERVER_UNAVAILABLE;
        if (mqtt_deserialize_connack(packet, packet_len, &session_present, &rc) == MQTT_OK) {
            if (rc == MQTT_CONNACK_ACCEPTED) {
                client->state = MQTT_STATE_CONNECTED;
            } else {
                client->state = MQTT_STATE_DISCONNECTED;
            }
            if (client->on_connect) {
                client->on_connect(client, rc, session_present, client->user_data);
            }
        }
        break;
    }

    case MQTT_PACKET_TYPE_PUBLISH: {
        mqtt_message_t msg;
        memset(&msg, 0, sizeof(msg));
        if (mqtt_deserialize_publish(packet, packet_len, &msg) == MQTT_OK) {
            /* QoS 1 受信時は自動で PUBACK を返信 */
            if (msg.qos == MQTT_QOS_1 && client->send_fn) {
                uint8_t ack_buf[4];
                size_t ack_len = 0;
                if (mqtt_serialize_puback(msg.packet_id, ack_buf, sizeof(ack_buf), &ack_len)) {
                    client_send(client, ack_buf, ack_len);
                }
            }

            if (client->on_message) {
                client->on_message(client, &msg, client->user_data);
            }
        }
        break;
    }

    case MQTT_PACKET_TYPE_PUBACK: {
        uint16_t packet_id = 0;
        if (mqtt_deserialize_puback(packet, packet_len, &packet_id) == MQTT_OK) {
            if (client->on_puback) {
                client->on_puback(client, packet_id, client->user_data);
            }
        }
        break;
    }

    case MQTT_PACKET_TYPE_SUBACK: {
        uint16_t packet_id = 0;
        mqtt_qos_t granted_qos = MQTT_QOS_0;
        if (mqtt_deserialize_suback(packet, packet_len, &packet_id, &granted_qos) == MQTT_OK) {
            if (client->on_suback) {
                client->on_suback(client, packet_id, granted_qos, client->user_data);
            }
        }
        break;
    }

    case MQTT_PACKET_TYPE_PINGRESP:
        /* PING応答受信 */
        break;

    default:
        break;
    }
}

void mqtt_client_feed_bytes(mqtt_client_t *client, const uint8_t *data, size_t len) {
    if (!client || !data || len == 0) return;

    if (client->rx_len + len > sizeof(client->rx_buffer)) {
        /* バッファ溢れ対策: リセット */
        client->rx_len = 0;
        return;
    }

    memcpy(&client->rx_buffer[client->rx_len], data, len);
    client->rx_len += len;

    /* バッファ内に完全なパケットが存在する限りループ処理 */
    while (client->rx_len >= 2) {
        mqtt_packet_type_t ptype;
        size_t total_packet_len = 0;

        mqtt_status_t st = mqtt_peek_packet_info(client->rx_buffer, client->rx_len, &ptype, &total_packet_len);
        if (st == MQTT_ERR_MALFORMED_PACKET) {
            /* パケットの境界が分からなくなったので、受信済みのバイトを捨てる */
            client->rx_len = 0;
            break;
        }
        if (st != MQTT_OK) {
            break; /* パケット受信完了待ち */
        }

        /* 1パケット処理 */
        process_single_packet(client, client->rx_buffer, total_packet_len);

        /* 処理済みパケットをバッファからシフト */
        size_t rem = client->rx_len - total_packet_len;
        if (rem > 0) {
            memmove(client->rx_buffer, &client->rx_buffer[total_packet_len], rem);
        }
        client->rx_len = rem;
    }
}

void mqtt_client_yield(mqtt_client_t *client, uint32_t current_time_sec) {
    if (!client || client->state != MQTT_STATE_CONNECTED) return;

    /* 前回の yield 以降に何か送っていれば、今を最後の送信時刻とみなす (接続直後もここで起点が決まる) */
    if (client->tx_since_yield) {
        client->last_activity_time = current_time_sec;
        client->tx_since_yield = false;
    }

    if (client->keep_alive_sec > 0) {
        if (current_time_sec - client->last_activity_time >= client->keep_alive_sec) {
            mqtt_client_ping(client);
            client->tx_since_yield = false;
            client->last_activity_time = current_time_sec;
        }
    }
}
