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

/* 出力バッファへの境界検査付き書き込み。1度でも溢れると ok=false になり以降は何も書かない */
typedef struct {
    uint8_t *buf;
    size_t cap;
    size_t len;
    bool ok;
} pkt_writer_t;

static void w_bytes(pkt_writer_t *w, const void *src, size_t n) {
    if (!w->ok) return;
    if (n > w->cap - w->len) {
        w->ok = false;
        return;
    }
    if (n > 0) memcpy(&w->buf[w->len], src, n);
    w->len += n;
}

static void w_u8(pkt_writer_t *w, uint8_t v) { w_bytes(w, &v, 1); }

static void w_u16(pkt_writer_t *w, size_t v) {
    if (v > 0xFFFF) {
        w->ok = false;
        return;
    }
    uint8_t b[2] = {(uint8_t)((v >> 8) & 0xFF), (uint8_t)(v & 0xFF)};
    w_bytes(w, b, 2);
}

static void w_utf8(pkt_writer_t *w, const char *str, size_t len) {
    w_u16(w, len);
    w_bytes(w, str, len);
}

/* PUBLISH のトピック名: 1 文字以上で、ワイルドカード('#' '+')を含まない
 * [MQTT-4.7.3-1] [MQTT-3.3.2-2] */
static bool topic_name_valid(const char *topic, size_t len) {
    if (len == 0) return false;
    for (size_t i = 0; i < len; ++i) {
        if (topic[i] == '#' || topic[i] == '+') return false;
    }
    return true;
}

/* Remaining Length のバイト数。符号化できない長さ(>268435455)は 0 */
static size_t remaining_length_bytes(size_t length) {
    if (length <= 127) return 1;
    if (length <= 16383) return 2;
    if (length <= 2097151) return 3;
    if (length <= 268435455) return 4;
    return 0;
}

/* 固定ヘッダ(1B + Remaining Length)を書く。出力全体が buf_size に収まらなければ何も書かず false */
static bool begin_packet(pkt_writer_t *w, uint8_t *buf, size_t buf_size, uint8_t header_byte,
                         size_t rem_len) {
    size_t rl_bytes = remaining_length_bytes(rem_len);
    w->buf = buf;
    w->cap = buf_size;
    w->len = 0;
    w->ok = false;
    if (rl_bytes == 0 || rem_len > buf_size || 1 + rl_bytes > buf_size - rem_len) return false;
    w->ok = true;
    w_u8(w, header_byte);
    uint8_t rl[4];
    mqtt_encode_remaining_length(rem_len, rl);
    w_bytes(w, rl, rl_bytes);
    return w->ok;
}

/* ---------------------------------------------------------
 * シリアライズ (エンコード)
 * --------------------------------------------------------- */
bool mqtt_serialize_connect(const mqtt_connect_options_t *options, uint8_t *buf, size_t buf_size, size_t *out_len) {
    if (!options || !options->client_id || !buf || !out_len) return false;

    const bool has_will = options->will_topic && options->will_message;
    const size_t id_len = strlen(options->client_id);
    const size_t will_topic_len = has_will ? strlen(options->will_topic) : 0;
    const size_t user_len = options->username ? strlen(options->username) : 0;
    const size_t pass_len = options->password ? strlen(options->password) : 0;

    /* 各フィールドは 2 バイト長プレフィックスで表現できる範囲に限る */
    if (id_len > 0xFFFF || will_topic_len > 0xFFFF || user_len > 0xFFFF || pass_len > 0xFFFF) return false;

    /* パスワードはユーザー名なしでは指定できない [MQTT-3.1.2-22] */
    if (options->password && !options->username) return false;
    /* 長さ 0 のクライアント ID は clean session のときだけ [MQTT-3.1.3-7] */
    if (id_len == 0 && !options->clean_session) return false;
    /* 遺言 QoS は 0..2 [MQTT-3.1.2-14] */
    if (has_will && options->will_qos > MQTT_QOS_2) return false;

    size_t rem_len = 10 + 2 + id_len;
    if (has_will) rem_len += 2 + will_topic_len + 2 + options->will_message_len;
    if (options->username) rem_len += 2 + user_len;
    if (options->password) rem_len += 2 + pass_len;

    pkt_writer_t w;
    if (!begin_packet(&w, buf, buf_size, (uint8_t)(MQTT_PACKET_TYPE_CONNECT << 4), rem_len)) return false;

    /* Variable Header: Protocol Name "MQTT" + Level 4 (v3.1.1) */
    static const uint8_t proto[] = {0x00, 0x04, 'M', 'Q', 'T', 'T', MQTT_PROTOCOL_VERSION_3_1_1};
    w_bytes(&w, proto, sizeof(proto));

    uint8_t flags = 0;
    if (options->clean_session) flags |= 0x02;
    if (has_will) {
        flags |= 0x04; /* Will Flag */
        flags |= (uint8_t)((options->will_qos & 0x03) << 3);
        if (options->will_retain) flags |= 0x20;
    }
    if (options->password) flags |= 0x40;
    if (options->username) flags |= 0x80;
    w_u8(&w, flags);

    uint16_t keep_alive = options->keep_alive_sec ? options->keep_alive_sec : MQTT_DEFAULT_KEEP_ALIVE;
    w_u16(&w, keep_alive);

    /* Payload */
    w_utf8(&w, options->client_id, id_len);
    if (has_will) {
        w_utf8(&w, options->will_topic, will_topic_len);
        w_u16(&w, options->will_message_len);
        w_bytes(&w, options->will_message, options->will_message_len);
    }
    if (options->username) w_utf8(&w, options->username, user_len);
    if (options->password) w_utf8(&w, options->password, pass_len);

    if (!w.ok || w.len != 1 + remaining_length_bytes(rem_len) + rem_len) return false;
    *out_len = w.len;
    return true;
}

bool mqtt_serialize_publish(const mqtt_message_t *msg, uint8_t *buf, size_t buf_size, size_t *out_len) {
    if (!msg || !msg->topic || !buf || !out_len) return false;
    if (msg->qos > MQTT_QOS_2) return false;
    if (msg->payload_len > 0 && !msg->payload) return false;

    size_t topic_len = msg->topic_len ? msg->topic_len : strlen(msg->topic);
    if (topic_len > 0xFFFF || msg->payload_len > SIZE_MAX / 2) return false;
    if (!topic_name_valid(msg->topic, topic_len)) return false;
    if (msg->qos > 0 && msg->packet_id == 0) return false; /* [MQTT-2.3.1-1] */
    size_t rem_len = 2 + topic_len + (msg->qos > 0 ? 2 : 0) + msg->payload_len;

    uint8_t header_byte = (uint8_t)(MQTT_PACKET_TYPE_PUBLISH << 4);
    if (msg->dup) header_byte |= 0x08;
    header_byte |= (uint8_t)((msg->qos & 0x03) << 1);
    if (msg->retain) header_byte |= 0x01;

    pkt_writer_t w;
    if (!begin_packet(&w, buf, buf_size, header_byte, rem_len)) return false;
    w_utf8(&w, msg->topic, topic_len);
    if (msg->qos > 0) w_u16(&w, msg->packet_id);
    w_bytes(&w, msg->payload, msg->payload_len);

    if (!w.ok) return false;
    *out_len = w.len;
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
    if (!topic || !buf || !out_len || qos > MQTT_QOS_2) return false;
    if (packet_id == 0) return false; /* パケット ID 0 は不正 [MQTT-2.3.1-1] */

    size_t topic_len = strlen(topic);
    if (topic_len == 0 || topic_len > 0xFFFF) return false; /* 空のフィルタは不正 [MQTT-4.7.3-1] */
    size_t rem_len = 2 + 2 + topic_len + 1; // Packet ID(2) + TopicLen(2) + Topic + QoS(1)

    pkt_writer_t w;
    if (!begin_packet(&w, buf, buf_size, (uint8_t)((MQTT_PACKET_TYPE_SUBSCRIBE << 4) | 0x02), rem_len)) return false;
    w_u16(&w, packet_id);
    w_utf8(&w, topic, topic_len);
    w_u8(&w, (uint8_t)(qos & 0x03));

    if (!w.ok) return false;
    *out_len = w.len;
    return true;
}

bool mqtt_serialize_unsubscribe(uint16_t packet_id, const char *topic, uint8_t *buf, size_t buf_size, size_t *out_len) {
    if (!topic || !buf || !out_len) return false;
    if (packet_id == 0) return false; /* パケット ID 0 は不正 [MQTT-2.3.1-1] */

    size_t topic_len = strlen(topic);
    if (topic_len == 0 || topic_len > 0xFFFF) return false; /* 空のフィルタは不正 [MQTT-4.7.3-1] */
    size_t rem_len = 2 + 2 + topic_len; // Packet ID(2) + TopicLen(2) + Topic

    pkt_writer_t w;
    if (!begin_packet(&w, buf, buf_size, (uint8_t)((MQTT_PACKET_TYPE_UNSUBSCRIBE << 4) | 0x02), rem_len)) return false;
    w_u16(&w, packet_id);
    w_utf8(&w, topic, topic_len);

    if (!w.ok) return false;
    *out_len = w.len;
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
        /* 4 バイト分そろっても継続ビットが立っていれば、Remaining Length は不正 (最大 4 バイト) */
        return len >= 5 ? MQTT_ERR_MALFORMED_PACKET : MQTT_ERR_INCOMPLETE_PACKET;
    }

    *total_packet_len = 1 + rem_len_bytes + rem_len;
    if (len < *total_packet_len) {
        return MQTT_ERR_INCOMPLETE_PACKET;
    }

    return MQTT_OK;
}

mqtt_status_t mqtt_deserialize_connack(const uint8_t *buf, size_t len, bool *session_present, mqtt_connack_rc_t *rc) {
    if (!buf || len < 4 || !session_present || !rc) return MQTT_ERR_INVALID_PARAM;
    /* 固定ヘッダは 0x20 (予約フラグは 0) [MQTT-2.2.2-2]。上位 7 ビットは予約 [MQTT-3.2.2-1]。
     * リターンコードは 0..5 */
    if (buf[0] != 0x20 || buf[1] != 2 || (buf[2] & 0xFE) != 0 || buf[3] > MQTT_CONNACK_NOT_AUTHORIZED) {
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
    const size_t packet_end = offset + rem_len; /* 以降の解析はこのパケット内に限定する */

    if (msg->qos > MQTT_QOS_2) return MQTT_ERR_MALFORMED_PACKET;
    if (rem_len < 2) return MQTT_ERR_MALFORMED_PACKET;

    /* Topic length */
    uint16_t topic_len = (uint16_t)((buf[offset] << 8) | buf[offset + 1]);
    offset += 2;

    if (topic_len > packet_end - offset) return MQTT_ERR_MALFORMED_PACKET;
    if (!topic_name_valid((const char *)&buf[offset], topic_len)) return MQTT_ERR_MALFORMED_PACKET;

    msg->topic = (const char *)&buf[offset];
    msg->topic_len = topic_len;
    offset += topic_len;

    /* Packet ID (QoS > 0) */
    if (msg->qos > 0) {
        if (packet_end - offset < 2) return MQTT_ERR_MALFORMED_PACKET;
        msg->packet_id = (uint16_t)((buf[offset] << 8) | buf[offset + 1]);
        if (msg->packet_id == 0) return MQTT_ERR_MALFORMED_PACKET; /* [MQTT-2.3.1-1] */
        offset += 2;
    } else {
        msg->packet_id = 0;
    }

    /* Payload */
    msg->payload = &buf[offset];
    msg->payload_len = packet_end - offset;

    return MQTT_OK;
}

mqtt_status_t mqtt_deserialize_puback(const uint8_t *buf, size_t len, uint16_t *packet_id) {
    if (!buf || len < 4 || !packet_id) return MQTT_ERR_INVALID_PARAM;
    if (buf[0] != (MQTT_PACKET_TYPE_PUBACK << 4) || buf[1] != 2) {
        return MQTT_ERR_MALFORMED_PACKET;
    }

    *packet_id = (uint16_t)((buf[2] << 8) | buf[3]);
    return MQTT_OK;
}

mqtt_status_t mqtt_deserialize_suback(const uint8_t *buf, size_t len, uint16_t *packet_id, mqtt_qos_t *granted_qos) {
    if (!buf || len < 5 || !packet_id || !granted_qos) return MQTT_ERR_INVALID_PARAM;
    if (buf[0] != (MQTT_PACKET_TYPE_SUBACK << 4) || buf[1] != 3) return MQTT_ERR_MALFORMED_PACKET;
    /* 許可される値は 0 / 1 / 2 と、失敗を表す 0x80 [MQTT-3.9.3] */
    if (buf[4] > MQTT_QOS_2 && buf[4] != 0x80) return MQTT_ERR_MALFORMED_PACKET;

    *packet_id = (uint16_t)((buf[2] << 8) | buf[3]);
    *granted_qos = (mqtt_qos_t)buf[4];
    return MQTT_OK;
}

mqtt_status_t mqtt_deserialize_unsuback(const uint8_t *buf, size_t len, uint16_t *packet_id) {
    if (!buf || len < 4 || !packet_id) return MQTT_ERR_INVALID_PARAM;
    if (buf[0] != (MQTT_PACKET_TYPE_UNSUBACK << 4) || buf[1] != 2) return MQTT_ERR_MALFORMED_PACKET;

    *packet_id = (uint16_t)((buf[2] << 8) | buf[3]);
    return MQTT_OK;
}
