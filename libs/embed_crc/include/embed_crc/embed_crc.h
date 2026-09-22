#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef enum { EMBED_CRC8_J1850, EMBED_CRC16_CCITT, EMBED_CRC16_MODBUS, EMBED_CRC16_XMODEM, EMBED_CRC32_IEEE, EMBED_CRC32C } embed_crc_kind;
/* update takes raw, unfinalized state; finish applies the algorithm xor-out. */
uint32_t embed_crc_init(embed_crc_kind kind);
uint32_t embed_crc_update(embed_crc_kind kind, uint32_t state, const void *data, size_t len);
uint32_t embed_crc_finish(embed_crc_kind kind, uint32_t state);
uint32_t embed_crc(embed_crc_kind kind, const void *data, size_t len);
uint16_t embed_fletcher16(uint16_t state, const void *data, size_t len);
uint32_t embed_adler32(uint32_t state, const void *data, size_t len);

#ifdef __cplusplus
}
#endif
