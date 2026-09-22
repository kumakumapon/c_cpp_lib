#include "embed_crc/embed_crc.h"
#include "crc_tables.h"
static uint32_t mask_for(embed_crc_kind k) {
    return k == EMBED_CRC8_J1850 ? 255u : k <= EMBED_CRC16_XMODEM ? 65535u : UINT32_MAX;
}
uint32_t embed_crc_init(embed_crc_kind k) { return k == EMBED_CRC16_XMODEM ? 0 : mask_for(k); }
uint32_t embed_crc_update(embed_crc_kind k, uint32_t state, const void *data, size_t len) {
    if (k < EMBED_CRC8_J1850 || k > EMBED_CRC32C || (!data && len))
        return state;
    const uint8_t *p = (const uint8_t *)data;
    unsigned bits = k == EMBED_CRC8_J1850 ? 8 : k <= EMBED_CRC16_XMODEM ? 16 : 32;
    bool reflected = k == EMBED_CRC16_MODBUS || bits == 32;
    uint32_t mask = mask_for(k);
    state &= mask;
    while (len--) {
#ifdef EMBED_CRC_TABLES
        unsigned table = k == EMBED_CRC16_XMODEM  ? 1u
                         : k > EMBED_CRC16_XMODEM ? (unsigned)k - 1
                                                  : (unsigned)k;
        if (reflected)
            state = (state >> 8) ^ crc_tables[table][(state ^ *p++) & 255];
        else
            state = ((state << 8) ^ crc_tables[table][((state >> (bits - 8)) ^ *p++) & 255]) & mask;
#else
        uint32_t poly = k == EMBED_CRC8_J1850     ? 0x1du
                        : k == EMBED_CRC16_MODBUS ? 0xa001u
                        : bits == 16              ? 0x1021u
                        : k == EMBED_CRC32_IEEE   ? 0xedb88320u
                                                  : 0x82f63b78u;
        state ^= reflected ? *p++ : (uint32_t)*p++ << (bits - 8);
        for (unsigned i = 0; i < 8; ++i) {
            if (reflected)
                state = (state >> 1) ^ ((state & 1) ? poly : 0);
            else
                state = ((state << 1) ^ ((state & (1u << (bits - 1))) ? poly : 0)) & mask;
        }
#endif
    }
    return state;
}
uint32_t embed_crc_finish(embed_crc_kind k, uint32_t s) {
    return (s ^ ((k == EMBED_CRC8_J1850 || k >= EMBED_CRC32_IEEE) ? mask_for(k) : 0)) & mask_for(k);
}
uint32_t embed_crc(embed_crc_kind k, const void *p, size_t n) {
    return embed_crc_finish(k, embed_crc_update(k, embed_crc_init(k), p, n));
}
uint16_t embed_fletcher16(uint16_t state, const void *data, size_t len) {
    if (!data && len)
        return state;
    const uint8_t *p = (const uint8_t *)data;
    unsigned a = state & 255, b = state >> 8;
    while (len--) {
        a = (a + *p++) % 255;
        b = (b + a) % 255;
    }
    return (uint16_t)((b << 8) | a);
}
uint32_t embed_adler32(uint32_t state, const void *data, size_t len) {
    if (!data && len)
        return state;
    const uint8_t *p = (const uint8_t *)data;
    uint32_t a = state & 65535, b = state >> 16;
    while (len--) {
        a = (a + *p++) % 65521;
        b = (b + a) % 65521;
    }
    return (b << 16) | a;
}
