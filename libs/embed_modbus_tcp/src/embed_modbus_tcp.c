#include "embed_modbus_tcp/embed_modbus_tcp.h"
#include "embed_modbus/modbus_rtu.h"
#include <string.h>
static uint16_t get16(const uint8_t *p) { return (uint16_t)(((uint16_t)p[0] << 8) | p[1]); }
static void put16(uint8_t *p, uint16_t n) {
    p[0] = (uint8_t)(n >> 8);
    p[1] = (uint8_t)n;
}
size_t embed_mbap_encode(const embed_mbap *h, uint8_t *dst, size_t cap) {
    if (!h || !dst || cap < 7 || h->length < 2 || h->length > 254)
        return 0;
    put16(dst, h->transaction);
    put16(dst + 2, 0);
    put16(dst + 4, h->length);
    dst[6] = h->unit;
    return 7;
}
size_t embed_mbap_decode(const uint8_t *src, size_t len, embed_mbap *h) {
    if (!src || !h || len < 7 || get16(src + 2) != 0 || get16(src + 4) < 2 || get16(src + 4) > 254)
        return 0;
    h->transaction = get16(src);
    h->length = get16(src + 4);
    h->unit = src[6];
    return 7;
}
size_t embed_modbus_tcp_handle(const modbus_slave_t *slave, const uint8_t *src, size_t len,
                               uint8_t *dst, size_t cap) {
    embed_mbap h;
    uint8_t pdu[253];
    size_t n = 0;
    if (!slave || !dst || cap < 260 || !embed_mbap_decode(src, len, &h) ||
        len != (size_t)h.length + 6)
        return 0;
    if (h.unit != slave->unit_id && h.unit != EMBED_MODBUS_TCP_UNIT_DIRECT && h.unit != 0)
        return 0;
    if (!modbus_slave_process_pdu(slave, src + 7, len - 7, pdu, &n) || n == 0 || n > 253)
        return 0;
    h.length = (uint16_t)(n + 1);
    embed_mbap_encode(&h, dst, cap);
    memcpy(dst + 7, pdu, n);
    return n + 7;
}
bool embed_modbus_tcp_frame(embed_modbus_tcp_connection *c, const uint8_t *data, size_t len,
                            embed_modbus_tcp_adu_fn on_adu, void *user) {
    if (!c || (!data && len) || !on_adu || c->failed || c->used >= sizeof(c->data))
        return false;
    while (len--) {
        c->data[c->used++] = *data++;
        if (c->used < 7)
            continue;
        embed_mbap h;
        if (!embed_mbap_decode(c->data, c->used, &h)) {
            c->failed = true;
            return false;
        }
        if (c->used == (size_t)h.length + 6) {
            /* コールバック中に同じ接続へ再入されても壊れないよう、先に空にしてからコピーを渡す */
            uint8_t adu[EMBED_MODBUS_TCP_MAX_ADU];
            const size_t n = c->used;
            memcpy(adu, c->data, n);
            c->used = 0;
            on_adu(adu, n, user);
        }
    }
    return true;
}
struct feed_ctx {
    const modbus_slave_t *slave;
    embed_modbus_tcp_send send;
    void *user;
};
static void serve_adu(const uint8_t *adu, size_t len, void *user) {
    const struct feed_ctx *f = (const struct feed_ctx *)user;
    uint8_t out[EMBED_MODBUS_TCP_MAX_ADU];
    size_t n = embed_modbus_tcp_handle(f->slave, adu, len, out, sizeof(out));
    if (n)
        f->send(out, n, f->user);
}
bool embed_modbus_tcp_feed(embed_modbus_tcp_connection *c, const modbus_slave_t *slave,
                           const uint8_t *data, size_t len, embed_modbus_tcp_send send,
                           void *user) {
    if (!slave || !send)
        return false;
    struct feed_ctx f = {slave, send, user};
    return embed_modbus_tcp_frame(c, data, len, serve_adu, &f);
}
size_t embed_modbus_rtu_tcp_handle(const modbus_slave_t *s, const uint8_t *src, size_t len,
                                   uint8_t *dst, size_t cap) {
    size_t n = 0;
    if (!s || !src || len < 4 || len > 256 || !dst || cap < 256)
        return 0;
    return modbus_rtu_slave_handle(s, src, len, dst, &n) ? n : 0;
}
size_t embed_modbus_tcp_build_request(uint16_t transaction, uint8_t unit, const uint8_t *pdu,
                                      size_t pdu_len, uint8_t *dst, size_t cap) {
    if (!pdu || pdu_len < 1 || pdu_len > 253 || !dst || cap < pdu_len + 7)
        return 0;
    embed_mbap h = {transaction, (uint16_t)(pdu_len + 1), unit};
    embed_mbap_encode(&h, dst, cap);
    memcpy(dst + 7, pdu, pdu_len);
    return pdu_len + 7;
}
modbus_status_t embed_modbus_tcp_parse_response(const uint8_t *adu, size_t len,
                                                uint16_t expected_transaction,
                                                uint8_t expected_unit, const uint8_t **pdu,
                                                size_t *pdu_len) {
    embed_mbap h;
    if (!pdu || !pdu_len)
        return MODBUS_ERR_INVALID_PARAM;
    if (!embed_mbap_decode(adu, len, &h) || len != (size_t)h.length + 6)
        return MODBUS_ERR_INVALID_RESPONSE;
    if (h.transaction != expected_transaction)
        return MODBUS_ERR_TRANSACTION_MISMATCH;
    if (h.unit != expected_unit)
        return MODBUS_ERR_ADDRESS_MISMATCH;
    *pdu = adu + 7;
    *pdu_len = len - 7;
    return MODBUS_OK;
}
bool embed_modbus_transaction_begin_at(embed_modbus_transactions *t, uint32_t now, uint16_t *id) {
    if (!t || !id)
        return false;
    unsigned slot;
    for (slot = 0; slot < EMBED_MODBUS_TCP_PENDING && t->used[slot]; ++slot) {
    }
    if (slot == EMBED_MODBUS_TCP_PENDING)
        return false;
    for (unsigned attempt = 0; attempt <= EMBED_MODBUS_TCP_PENDING; ++attempt) {
        uint16_t candidate = t->next++;
        bool found = false;
        for (unsigned i = 0; i < EMBED_MODBUS_TCP_PENDING; ++i)
            if (t->used[i] && t->ids[i] == candidate)
                found = true;
        if (!found) {
            t->ids[slot] = candidate;
            t->used[slot] = true;
            t->timed[slot] = true;
            t->started[slot] = now;
            *id = candidate;
            return true;
        }
    }
    return false;
}
bool embed_modbus_transaction_begin(embed_modbus_transactions *t, uint16_t *id) {
    if (!embed_modbus_transaction_begin_at(t, 0, id))
        return false;
    for (unsigned i = 0; i < EMBED_MODBUS_TCP_PENDING; ++i)
        if (t->used[i] && t->ids[i] == *id)
            t->timed[i] = false; /* 時刻なしで始めた ID は expire の対象外 */
    return true;
}
bool embed_modbus_transaction_end(embed_modbus_transactions *t, uint16_t id) {
    if (!t)
        return false;
    for (unsigned i = 0; i < EMBED_MODBUS_TCP_PENDING; ++i)
        if (t->used[i] && t->ids[i] == id) {
            t->used[i] = false;
            return true;
        }
    return false;
}
size_t embed_modbus_transaction_expire(embed_modbus_transactions *t, uint32_t now,
                                       uint32_t timeout, uint16_t *expired, size_t cap) {
    if (!t)
        return 0;
    size_t n = 0;
    for (unsigned i = 0; i < EMBED_MODBUS_TCP_PENDING; ++i) {
        if (!t->used[i] || !t->timed[i] || (uint32_t)(now - t->started[i]) < timeout)
            continue;
        if (expired && n >= cap)
            break; /* 入りきらない分は次回へ残す */
        if (expired)
            expired[n] = t->ids[i];
        t->used[i] = false;
        ++n;
    }
    return n;
}
size_t embed_modbus_transaction_pending(const embed_modbus_transactions *t) {
    size_t n = 0;
    if (t)
        for (unsigned i = 0; i < EMBED_MODBUS_TCP_PENDING; ++i)
            n += t->used[i] ? 1u : 0u;
    return n;
}
