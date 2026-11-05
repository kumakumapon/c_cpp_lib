#include "embed_frame/embed_frame.h"

bool embed_slip_encode(const uint8_t *src, size_t len, uint8_t *dst, size_t cap, size_t *out_len) {
    if ((!src && len) || !dst || !out_len)
        return false;
    size_t o = 0;
    if (cap < 2)
        return false;
    dst[o++] = EMBED_SLIP_END;
    for (size_t i = 0; i < len; ++i) {
        const uint8_t b = src[i];
        const bool esc = b == EMBED_SLIP_END || b == EMBED_SLIP_ESC;
        if (o + (esc ? 2u : 1u) + 1u > cap) /* 末尾の END の分も残す */
            return false;
        if (esc) {
            dst[o++] = EMBED_SLIP_ESC;
            dst[o++] = b == EMBED_SLIP_END ? EMBED_SLIP_ESC_END : EMBED_SLIP_ESC_ESC;
        } else {
            dst[o++] = b;
        }
    }
    dst[o++] = EMBED_SLIP_END;
    *out_len = o;
    return true;
}

void embed_slip_decoder_init(embed_slip_decoder_t *d, uint8_t *buf, size_t cap) {
    if (!d)
        return;
    d->buf = buf;
    d->cap = buf ? cap : 0;
    d->len = 0;
    d->escape = false;
    d->error = false;
    d->frames = 0;
    d->errors = 0;
}

void embed_slip_decoder_reset(embed_slip_decoder_t *d) {
    if (!d)
        return;
    d->len = 0;
    d->escape = false;
    d->error = false;
}

embed_frame_status embed_slip_decoder_feed(embed_slip_decoder_t *d, uint8_t byte,
                                           const uint8_t **frame, size_t *len) {
    if (!d || !d->buf || !frame || !len)
        return EMBED_FRAME_ERROR;
    if (byte == EMBED_SLIP_END) {
        const size_t n = d->len;
        /* ESC の直後に END が来たら不完全なエスケープ */
        const bool bad = d->error || d->escape;
        d->len = 0;
        d->escape = false;
        d->error = false;
        if (bad) {
            d->errors++;
            return EMBED_FRAME_ERROR;
        }
        if (n == 0)
            return EMBED_FRAME_IN_PROGRESS;
        d->frames++;
        *frame = d->buf;
        *len = n;
        return EMBED_FRAME_COMPLETE;
    }
    if (d->error)
        return EMBED_FRAME_IN_PROGRESS; /* END まで読み捨てる */
    if (d->escape) {
        d->escape = false;
        if (byte == EMBED_SLIP_ESC_END)
            byte = EMBED_SLIP_END;
        else if (byte == EMBED_SLIP_ESC_ESC)
            byte = EMBED_SLIP_ESC;
        else {
            d->error = true; /* RFC 1055 で定義されないエスケープ */
            return EMBED_FRAME_IN_PROGRESS;
        }
    } else if (byte == EMBED_SLIP_ESC) {
        d->escape = true;
        return EMBED_FRAME_IN_PROGRESS;
    }
    if (d->len >= d->cap) {
        d->error = true;
        return EMBED_FRAME_IN_PROGRESS;
    }
    d->buf[d->len++] = byte;
    return EMBED_FRAME_IN_PROGRESS;
}
