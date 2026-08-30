#include "embed_mqtt/mqtt_packet.h"
#include <string.h>

/* ---------------------------------------------------------
 * 可変長残り長 (Remaining Length) 処理
 * --------------------------------------------------------- */
size_t mqtt_encode_remaining_length(size_t length, uint8_t *dest) {
    size_t bytes = 0;
    do {
        uint8_t d = (uint8_t)(length % 128);
        length /= 128;
        if (length > 0) {
            d |= 0x80;
        }
        dest[bytes++] = d;
    } while (length > 0 && bytes < 4);

    return bytes;
}

size_t mqtt_decode_remaining_length(const uint8_t *src, size_t max_len, size_t *value) {
    size_t multiplier = 1;
    size_t len = 0;
    size_t bytes = 0;
    uint8_t digit;

    do {
        if (bytes >= max_len || bytes >= 4) {
            return 0; /* 不正または不完全 */
        }
        digit = src[bytes++];
        len += (digit & 127) * multiplier;
        multiplier *= 128;
    } while ((digit & 128) != 0);

    *value = len;
    return bytes;
}

static void write_utf8_string(uint8_t *buf, size_t *offset, const char *str) {
    uint16_t len = (uint16_t)strlen(str);
    buf[(*offset)++] = (uint8_t)((len >> 8) & 0xFF);
    buf[(*offset)++] = (uint8_t)(len & 0xFF);
    memcpy(&buf[*offset], str, len);
    *offset += len;
}

/* ---------------------------------------------------------
 * シリアライズ (エンコード)
 * --------------------------------------------------------- */
bool mqtt_serialize_connect(const mqtt_connect_options_t *options, uint8_t *buf, size_t buf_size, size_t *out_len) {
    if (!options || !options->client_id || !buf || !out_len) return false;

    uint8_t var_header_and_payload[MQTT_MAX_PACKET_SIZE];
    size_t v_len = 0;

    /* Variable Header */
    /* Protocol Name: "MQTT" */
    var_header_and_payload[v_len++] = 0x00;
    var_header_and_payload[v_len++] = 0x04;
    var_header_and_payload[v_len++] = 'M';
    var_header_and_payload[v_len++] = 'Q';
    var_header_and_payload[v_len++] = 'T';
    var_header_and_payload[v_len++] = 'T';

    /* Protocol Level: 4 (v3.1.1) */
    var_header_and_payload[v_len++] = MQTT_PROTOCOL_VERSION_3_1_1;

    /* Connect Flags */
    uint8_t flags = 0;
    if (options->clean_session) flags |= 0x02;
    if (options->will_topic && options->will_message) {
        flags |= 0x04; // Will Flag
        flags |= (uint8_t)((options->will_qos & 0x03) << 3);
        if (options->will_retain) flags |= 0x20;
    }
    if (options->password) flags |= 0x40;
    if (options->username) flags |= 0x80;
    var_header_and_payload[v_len++] = flags;

    /* Keep Alive (2 bytes) */
    uint16_t keep_alive = options->keep_alive_sec ? options->keep_alive_sec : MQTT_DEFAULT_KEEP_ALIVE;
    var_header_and_payload[v_len++] = (uint8_t)((keep_alive >> 8) & 0xFF);
    var_header_and_payload[v_len++] = (uint8_t)(keep_alive & 0xFF);

    /* Payload */
    /* 1. Client ID */
    write_utf8_string(var_header_and_payload, &v_len, options->client_id);

    /* 2. Will Topic & Message */
    if (options->will_topic && options->will_message) {
        write_utf8_string(var_header_and_payload, &v_len, options->will_topic);
        var_header_and_payload[v_len++] = (uint8_t)((options->will_message_len >> 8) & 0xFF);
        var_header_and_payload[v_len++] = (uint8_t)(options->will_message_len & 0xFF);
        memcpy(&var_header_and_payload[v_len], options->will_message, options->will_message_len);
        v_len += options->will_message_len;
    }

    /* 3. Username */
    if (options->username) {
        write_utf8_string(var_header_and_payload, &v_len, options->username);
    }

    /* 4. Password */
    if (options->password) {
        write_utf8_string(var_header_and_payload, &v_len, options->password);
    }

    /* Fixed Header */
    buf[0] = (uint8_t)(MQTT_PACKET_TYPE_CONNECT << 4);
    size_t rem_len_bytes = mqtt_encode_remaining_length(v_len, &buf[1]);

    if (1 + rem_len_bytes + v_len > buf_size) {
        return false;
    }

    memcpy(&buf[1 + rem_len_bytes], var_header_and_payload, v_len);
    *out_len = 1 + rem_len_bytes + v_len;
    return true;
}

bool mqtt_serialize_publish(const mqtt_message_t *msg, uint8_t *buf, size_t buf_size, size_t *out_len) {
    if (!msg || !msg->topic || !buf || !out_len) return false;

    uint16_t topic_len = (uint16_t)(msg->topic_len ? msg->topic_len : strlen(msg->topic));
    size_t rem_len = 2 + topic_len + (msg->qos > 0 ? 2 : 0) + msg->payload_len;

    /* Fixed Header Byte 1 */
    uint8_t header_byte = (uint8_t)(MQTT_PACKET_TYPE_PUBLISH << 4);
    if (msg->dup) header_byte |= 0x08;
    header_byte |= (uint8_t)((msg->qos & 0x03) << 1);
    if (msg->retain) header_byte |= 0x01;
    buf[0] = header_byte;

    size_t rem_len_bytes = mqtt_encode_remaining_length(rem_len, &buf[1]);
    size_t offset = 1 + rem_len_bytes;

    if (offset + rem_len > buf_size) {
        return false;
    }

    /* Topic Name */
    buf[offset++] = (uint8_t)((topic_len >> 8) & 0xFF);
    buf[offset++] = (uint8_t)(topic_len & 0xFF);
    memcpy(&buf[offset], msg->topic, topic_len);
    offset += topic_len;

    /* Packet Identifier (QoS > 0 のみ) */
    if (msg->qos > 0) {
        buf[offset++] = (uint8_t)((msg->packet_id >> 8) & 0xFF);
        buf[offset++] = (uint8_t)(msg->packet_id & 0xFF);
    }

    /* Payload */
    if (msg->payload_len > 0 && msg->payload) {
        memcpy(&buf[offset], msg->payload, msg->payload_len);
        offset += msg->payload_len;
    }

    *out_len = offset;
    return true;
}

bool mqtt_serialize_puback(uint16_t packet_id, uint8_t *buf, size_t buf_size, size_t *out_len) {
    if (!buf || buf_size < 4 || !out_len) return false;
    buf[0] = (uint8_t)(MQTT_PACKET_TYPE_PUBACK << 4);
    buf[1] = 0x02; // Remaining length = 2
    buf[2] = (uint8_t)((packet_id >> 8) & 0xFF);
    buf[3] = (uint8_t)(packet_id & 0xFF);
    *out_len = 4;
    return true;
}

bool mqtt_serialize_subscribe(uint16_t packet_id, const char *topic, mqtt_qos_t qos, uint8_t *buf, size_t buf_size, size_t *out_len) {
    if (!topic || !buf || !out_len) return false;

    uint16_t topic_len = (uint16_t)strlen(topic);
    size_t rem_len = 2 + 2 + topic_len + 1; // Packet ID(2) + TopicLen(2) + Topic + QoS(1)

    buf[0] = (uint8_t)((MQTT_PACKET_TYPE_SUBSCRIBE << 4) | 0x02);
    size_t rem_len_bytes = mqtt_encode_remaining_length(rem_len, &buf[1]);
    size_t offset = 1 + rem_len_bytes;

    if (offset + rem_len > buf_size) return false;

    /* Packet ID */
    buf[offset++] = (uint8_t)((packet_id >> 8) & 0xFF);
    buf[offset++] = (uint8_t)(packet_id & 0xFF);

    /* Topic */
    buf[offset++] = (uint8_t)((topic_len >> 8) & 0xFF);
    buf[offset++] = (uint8_t)(topic_len & 0xFF);
    memcpy(&buf[offset], topic, topic_len);
    offset += topic_len;

    /* Requested QoS */
    buf[offset++] = (uint8_t)(qos & 0x03);

    *out_len = offset;
    return true;
}

bool mqtt_serialize_unsubscribe(uint16_t packet_id, const char *topic, uint8_t *buf, size_t buf_size, size_t *out_len) {
    if (!topic || !buf || !out_len) return false;

    uint16_t topic_len = (uint16_t)strlen(topic);
    size_t rem_len = 2 + 2 + topic_len; // Packet ID(2) + TopicLen(2) + Topic

    buf[0] = (uint8_t)((MQTT_PACKET_TYPE_UNSUBSCRIBE << 4) | 0x02);
    size_t rem_len_bytes = mqtt_encode_remaining_length(rem_len, &buf[1]);
    size_t offset = 1 + rem_len_bytes;

    if (offset + rem_len > buf_size) return false;

    buf[offset++] = (uint8_t)((packet_id >> 8) & 0xFF);
    buf[offset++] = (uint8_t)(packet_id & 0xFF);

    buf[offset++] = (uint8_t)((topic_len >> 8) & 0xFF);
    buf[offset++] = (uint8_t)(topic_len & 0xFF);
    memcpy(&buf[offset], topic, topic_len);
    offset += topic_len;

    *out_len = offset;
    return true;
}

bool mqtt_serialize_pingreq(uint8_t *buf, size_t buf_size, size_t *out_len) {
    if (!buf || buf_size < 2 || !out_len) return false;
    buf[0] = (uint8_t)(MQTT_PACKET_TYPE_PINGREQ << 4);
    buf[1] = 0x00;
    *out_len = 2;
    return true;
}

bool mqtt_serialize_disconnect(uint8_t *buf, size_t buf_size, size_t *out_len) {
    if (!buf || buf_size < 2 || !out_len) return false;
    buf[0] = (uint8_t)(MQTT_PACKET_TYPE_DISCONNECT << 4);
    buf[1] = 0x00;
    *out_len = 2;
    return true;
}

/* ---------------------------------------------------------
 * デシリアライズ (デコード)
 * --------------------------------------------------------- */
mqtt_status_t mqtt_peek_packet_info(const uint8_t *buf, size_t len,
                                    mqtt_packet_type_t *type, size_t *total_packet_len) {
    if (!buf || len < 2 || !type || !total_packet_len) return MQTT_ERR_INCOMPLETE_PACKET;

    *type = (mqtt_packet_type_t)(buf[0] >> 4);

    size_t rem_len = 0;
    size_t rem_len_bytes = mqtt_decode_remaining_length(&buf[1], len - 1, &rem_len);
    if (rem_len_bytes == 0) {
        return MQTT_ERR_INCOMPLETE_PACKET;
    }

    *total_packet_len = 1 + rem_len_bytes + rem_len;
    if (len < *total_packet_len) {
        return MQTT_ERR_INCOMPLETE_PACKET;
    }

    return MQTT_OK;
}

mqtt_status_t mqtt_deserialize_connack(const uint8_t *buf, size_t len, bool *session_present, mqtt_connack_rc_t *rc) {
    if (!buf || len < 4 || !session_present || !rc) return MQTT_ERR_INVALID_PARAM;
    if ((buf[0] >> 4) != MQTT_PACKET_TYPE_CONNACK || buf[1] != 2) {
        return MQTT_ERR_MALFORMED_PACKET;
    }

    *session_present = (buf[2] & 0x01) != 0;
    *rc = (mqtt_connack_rc_t)buf[3];
    return MQTT_OK;
}

mqtt_status_t mqtt_deserialize_publish(const uint8_t *buf, size_t len, mqtt_message_t *msg) {
    if (!buf || len < 3 || !msg) return MQTT_ERR_INVALID_PARAM;
    if ((buf[0] >> 4) != MQTT_PACKET_TYPE_PUBLISH) return MQTT_ERR_MALFORMED_PACKET;

    msg->dup    = (buf[0] & 0x08) != 0;
    msg->qos    = (mqtt_qos_t)((buf[0] >> 1) & 0x03);
    msg->retain = (buf[0] & 0x01) != 0;

    size_t rem_len = 0;
    size_t rem_len_bytes = mqtt_decode_remaining_length(&buf[1], len - 1, &rem_len);
    if (rem_len_bytes == 0 || len < 1 + rem_len_bytes + rem_len) {
        return MQTT_ERR_INCOMPLETE_PACKET;
    }

    size_t offset = 1 + rem_len_bytes;

    /* Topic length */
    uint16_t topic_len = (uint16_t)((buf[offset] << 8) | buf[offset + 1]);
    offset += 2;

    if (offset + topic_len > len) return MQTT_ERR_MALFORMED_PACKET;

    msg->topic = (const char *)&buf[offset];
    msg->topic_len = topic_len;
    offset += topic_len;

    /* Packet ID (QoS > 0) */
    if (msg->qos > 0) {
        if (offset + 2 > len) return MQTT_ERR_MALFORMED_PACKET;
        msg->packet_id = (uint16_t)((buf[offset] << 8) | buf[offset + 1]);
        offset += 2;
    } else {
        msg->packet_id = 0;
    }

    /* Payload */
    size_t total_header_len = offset;
    size_t full_packet_len = 1 + rem_len_bytes + rem_len;
    msg->payload = &buf[offset];
    msg->payload_len = full_packet_len - total_header_len;

    return MQTT_OK;
}

mqtt_status_t mqtt_deserialize_puback(const uint8_t *buf, size_t len, uint16_t *packet_id) {
    if (!buf || len < 4 || !packet_id) return MQTT_ERR_INVALID_PARAM;
    if ((buf[0] >> 4) != MQTT_PACKET_TYPE_PUBACK || buf[1] != 2) {
        return MQTT_ERR_MALFORMED_PACKET;
    }

    *packet_id = (uint16_t)((buf[2] << 8) | buf[3]);
    return MQTT_OK;
}

mqtt_status_t mqtt_deserialize_suback(const uint8_t *buf, size_t len, uint16_t *packet_id, mqtt_qos_t *granted_qos) {
    if (!buf || len < 5 || !packet_id || !granted_qos) return MQTT_ERR_INVALID_PARAM;
    if ((buf[0] >> 4) != MQTT_PACKET_TYPE_SUBACK) return MQTT_ERR_MALFORMED_PACKET;

    *packet_id = (uint16_t)((buf[2] << 8) | buf[3]);
    *granted_qos = (mqtt_qos_t)buf[4];
    return MQTT_OK;
}

mqtt_status_t mqtt_deserialize_unsuback(const uint8_t *buf, size_t len, uint16_t *packet_id) {
    if (!buf || len < 4 || !packet_id) return MQTT_ERR_INVALID_PARAM;
    if ((buf[0] >> 4) != MQTT_PACKET_TYPE_UNSUBACK) return MQTT_ERR_MALFORMED_PACKET;

    *packet_id = (uint16_t)((buf[2] << 8) | buf[3]);
    return MQTT_OK;
}
