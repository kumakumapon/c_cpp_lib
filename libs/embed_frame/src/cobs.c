#include "embed_frame/embed_frame.h"

bool embed_cobs_encode(const uint8_t *src, size_t len, uint8_t *dst, size_t cap, size_t *out_len) {
    if ((!src && len) || !dst || !out_len || cap < 1)
        return false;
    size_t code_at = 0; /* 現在のブロックのコードバイトの位置 */
    size_t out = 1;
    uint8_t code = 1;
    bool open = true; /* コードバイトが未確定のブロックがある */
    for (size_t i = 0; i < len; ++i) {
        const uint8_t b = src[i];
        if (b != 0) {
            if (out >= cap)
                return false;
            dst[out++] = b;
            ++code;
        }
        if (b == 0 || code == 0xFF) {
            dst[code_at] = code;
            code = 1;
            /* 0 の後、または入力が続くなら次のブロックのコードバイトを確保する。
             * 254 バイトの非 0 ブロックで入力が終わった場合は確保しない (正準形) */
            if (b == 0 || i + 1 < len) {
                if (out >= cap)
                    return false;
                code_at = out++;
                open = true;
            } else {
                open = false;
            }
        }
    }
    if (open)
        dst[code_at] = code;
    *out_len = out;
    return true;
}

bool embed_cobs_decode(const uint8_t *src, size_t len, uint8_t *dst, size_t cap, size_t *out_len) {
    if (!src || len == 0 || !dst || !out_len)
        return false;
    size_t i = 0, o = 0;
    while (i < len) {
        const uint8_t code = src[i++];
        if (code == 0)
            return false; /* 符号化データに 0x00 は現れない */
        const size_t n = (size_t)code - 1u;
        if (n > len - i)
            return false; /* ブロックが途中で切れている */
        for (size_t k = 0; k < n; ++k) {
            const uint8_t b = src[i++];
            if (b == 0 || o >= cap)
                return false;
            dst[o++] = b; /* o < i なのでその場復号でも未読の入力を壊さない */
        }
        if (code != 0xFF && i < len) {
            if (o >= cap)
                return false;
            dst[o++] = 0;
        }
    }
    *out_len = o;
    return true;
}

void embed_cobs_decoder_init(embed_cobs_decoder_t *d, uint8_t *buf, size_t cap) {
    if (!d)
        return;
    d->buf = buf;
    d->cap = buf ? cap : 0;
    d->len = 0;
    d->overflow = false;
    d->frames = 0;
    d->errors = 0;
}

void embed_cobs_decoder_reset(embed_cobs_decoder_t *d) {
    if (!d)
        return;
    d->len = 0;
    d->overflow = false;
}

embed_frame_status embed_cobs_decoder_feed(embed_cobs_decoder_t *d, uint8_t byte,
                                           const uint8_t **frame, size_t *len) {
    if (!d || !d->buf || !frame || !len)
        return EMBED_FRAME_ERROR;
    if (byte != EMBED_COBS_DELIMITER) {
        if (d->len >= d->cap)
            d->overflow = true; /* 区切りまで読み捨てる */
        else
            d->buf[d->len++] = byte;
        return EMBED_FRAME_IN_PROGRESS;
    }
    const size_t n = d->len;
    const bool overflow = d->overflow;
    d->len = 0;
    d->overflow = false;
    if (n == 0 && !overflow)
        return EMBED_FRAME_IN_PROGRESS; /* 区切りの連続 (同期用) */
    size_t out = 0;
    if (overflow || !embed_cobs_decode(d->buf, n, d->buf, d->cap, &out)) {
        d->errors++;
        return EMBED_FRAME_ERROR;
    }
    d->frames++;
    *frame = d->buf;
    *len = out;
    return EMBED_FRAME_COMPLETE;
}
