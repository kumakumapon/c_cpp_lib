#include "embed_crc/embed_crc.h"
#include "crc_tables.h"

static const embed_crc_params_t catalog[EMBED_CRC_KIND_COUNT] = {
    /* width, reflect, poly, init, xorout */
    [EMBED_CRC8_J1850] = {8, false, 0x1Du, 0xFFu, 0xFFu},
    [EMBED_CRC16_CCITT] = {16, false, 0x1021u, 0xFFFFu, 0x0000u},
    [EMBED_CRC16_MODBUS] = {16, true, 0x8005u, 0xFFFFu, 0x0000u},
    [EMBED_CRC16_XMODEM] = {16, false, 0x1021u, 0x0000u, 0x0000u},
    [EMBED_CRC32_IEEE] = {32, true, 0x04C11DB7u, 0xFFFFFFFFu, 0xFFFFFFFFu},
    [EMBED_CRC32C] = {32, true, 0x1EDC6F41u, 0xFFFFFFFFu, 0xFFFFFFFFu},
    [EMBED_CRC8_SMBUS] = {8, false, 0x07u, 0x00u, 0x00u},
    [EMBED_CRC8_MAXIM] = {8, true, 0x31u, 0x00u, 0x00u},
    [EMBED_CRC8_NRSC5] = {8, false, 0x31u, 0xFFu, 0x00u},
    [EMBED_CRC16_KERMIT] = {16, true, 0x1021u, 0x0000u, 0x0000u},
    [EMBED_CRC16_X25] = {16, true, 0x1021u, 0xFFFFu, 0xFFFFu},
    [EMBED_CRC32_MPEG2] = {32, false, 0x04C11DB7u, 0xFFFFFFFFu, 0x00000000u},
};

static bool kind_valid(embed_crc_kind k) { return (unsigned)k < (unsigned)EMBED_CRC_KIND_COUNT; }
static bool params_valid(const embed_crc_params_t *p) {
    return p && p->width >= 1 && p->width <= 32;
}
static uint32_t mask_of(unsigned width) {
    return width >= 32 ? UINT32_MAX : (UINT32_C(1) << width) - 1u;
}
static uint32_t reflect_bits(uint32_t v, unsigned width) {
    uint32_t r = 0;
    for (unsigned i = 0; i < width; ++i) {
        r = (r << 1) | (v & 1u);
        v >>= 1;
    }
    return r;
}

uint32_t embed_crc_params_init(const embed_crc_params_t *p) {
    if (!params_valid(p))
        return 0;
    uint32_t init = p->init & mask_of(p->width);
    return p->reflect ? reflect_bits(init, p->width) : init;
}

uint32_t embed_crc_params_update(const embed_crc_params_t *p, uint32_t state, const void *data,
                                 size_t len) {
    if (!params_valid(p) || (!data && len))
        return state;
    const uint8_t *d = (const uint8_t *)data;
    const unsigned w = p->width;
    const uint32_t mask = mask_of(w);
    state &= mask;
    if (p->reflect) {
        const uint32_t rpoly = reflect_bits(p->poly & mask, w);
        while (len--) {
            state ^= *d++;
            for (unsigned i = 0; i < 8; ++i)
                state = (state & 1u) ? (state >> 1) ^ rpoly : state >> 1;
        }
    } else {
        const uint32_t poly = p->poly & mask;
        while (len--) {
            const uint8_t byte = *d++;
            for (int i = 7; i >= 0; --i) {
                const uint32_t bit = ((state >> (w - 1)) ^ ((uint32_t)byte >> i)) & 1u;
                state = (state << 1) & mask;
                if (bit)
                    state ^= poly;
            }
        }
    }
    return state;
}

uint32_t embed_crc_params_finish(const embed_crc_params_t *p, uint32_t state) {
    if (!params_valid(p))
        return 0;
    return (state ^ p->xorout) & mask_of(p->width);
}

uint32_t embed_crc_params_compute(const embed_crc_params_t *p, const void *data, size_t len) {
    return embed_crc_params_finish(p, embed_crc_params_update(p, embed_crc_params_init(p), data, len));
}

bool embed_crc_get_params(embed_crc_kind k, embed_crc_params_t *out) {
    if (!kind_valid(k) || !out)
        return false;
    *out = catalog[k];
    return true;
}

uint32_t embed_crc_init(embed_crc_kind k) {
    return kind_valid(k) ? embed_crc_params_init(&catalog[k]) : 0;
}

#ifdef EMBED_CRC_TABLES
static const void *table_for(embed_crc_kind k) {
    switch (k) {
    case EMBED_CRC8_J1850:
        return crc_t8_1d;
    case EMBED_CRC8_SMBUS:
        return crc_t8_07;
    case EMBED_CRC8_NRSC5:
        return crc_t8_31;
    case EMBED_CRC8_MAXIM:
        return crc_t8_31r;
    case EMBED_CRC16_CCITT:
    case EMBED_CRC16_XMODEM:
        return crc_t16_1021;
    case EMBED_CRC16_MODBUS:
        return crc_t16_8005r;
    case EMBED_CRC16_KERMIT:
    case EMBED_CRC16_X25:
        return crc_t16_1021r;
    case EMBED_CRC32_MPEG2:
        return crc_t32_04c11db7;
    case EMBED_CRC32_IEEE:
        return crc_t32_04c11db7r;
    case EMBED_CRC32C:
        return crc_t32_1edc6f41r;
    default:
        return 0;
    }
}
static uint32_t entry(unsigned width, const void *t, unsigned i) {
    return width == 8    ? ((const uint8_t *)t)[i]
           : width == 16 ? ((const uint16_t *)t)[i]
                         : ((const uint32_t *)t)[i];
}
#endif

uint32_t embed_crc_update(embed_crc_kind k, uint32_t state, const void *data, size_t len) {
    if (!kind_valid(k))
        return state;
#ifdef EMBED_CRC_TABLES
    if (!data && len)
        return state;
    const embed_crc_params_t *p = &catalog[k];
    const void *t = table_for(k);
    const unsigned w = p->width;
    const uint32_t mask = mask_of(w);
    const uint8_t *d = (const uint8_t *)data;
    state &= mask;
    while (len--) {
        if (p->reflect)
            state = (state >> 8) ^ entry(w, t, (state ^ *d++) & 0xFFu);
        else
            state = ((state << 8) ^ entry(w, t, ((state >> (w - 8)) ^ *d++) & 0xFFu)) & mask;
    }
    return state;
#else
    return embed_crc_params_update(&catalog[k], state, data, len);
#endif
}

uint32_t embed_crc_finish(embed_crc_kind k, uint32_t state) {
    return kind_valid(k) ? embed_crc_params_finish(&catalog[k], state) : 0;
}

uint32_t embed_crc(embed_crc_kind k, const void *data, size_t len) {
    return embed_crc_finish(k, embed_crc_update(k, embed_crc_init(k), data, len));
}

size_t embed_crc_append(embed_crc_kind k, uint32_t crc, uint8_t *dst, size_t cap) {
    if (!kind_valid(k) || !dst)
        return 0;
    const size_t n = catalog[k].width / 8u;
    if (cap < n)
        return 0;
    for (size_t i = 0; i < n; ++i) {
        const size_t shift = catalog[k].reflect ? 8u * i : 8u * (n - 1u - i);
        dst[i] = (uint8_t)(crc >> shift);
    }
    return n;
}

bool embed_crc_verify(embed_crc_kind k, const void *frame, size_t len) {
    if (!kind_valid(k) || !frame)
        return false;
    const size_t n = catalog[k].width / 8u;
    if (len < n)
        return false;
    uint8_t expected[4];
    const uint32_t crc = embed_crc(k, frame, len - n);
    embed_crc_append(k, crc, expected, sizeof(expected));
    const uint8_t *tail = (const uint8_t *)frame + (len - n);
    uint8_t diff = 0;
    for (size_t i = 0; i < n; ++i)
        diff |= (uint8_t)(tail[i] ^ expected[i]);
    return diff == 0;
}

uint16_t embed_fletcher16(uint16_t state, const void *data, size_t len) {
    if (!data && len)
        return state;
    const uint8_t *p = (const uint8_t *)data;
    unsigned a = state & 255u, b = state >> 8;
    while (len--) {
        a = (a + *p++) % 255u;
        b = (b + a) % 255u;
    }
    return (uint16_t)((b << 8) | a);
}

uint32_t embed_adler32(uint32_t state, const void *data, size_t len) {
    if (!data && len)
        return state;
    /* NMAX: 剰余を遅らせても b が 32bit を超えない最大ブロック長 (zlib と同じ値) */
    enum { MOD = 65521, NMAX = 5552 };
    const uint8_t *p = (const uint8_t *)data;
    uint32_t a = state & 0xFFFFu, b = state >> 16;
    while (len) {
        size_t block = len < NMAX ? len : NMAX;
        len -= block;
        while (block--) {
            a += *p++;
            b += a;
        }
        a %= MOD;
        b %= MOD;
    }
    return (b << 16) | a;
}
