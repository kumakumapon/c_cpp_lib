#include "uart_protocol/uart_crc.h"
#include "embed_crc/embed_crc.h"
uint16_t uart_crc16_update(uint16_t crc, uint8_t byte) {
    return (uint16_t)embed_crc_update(EMBED_CRC16_CCITT, crc, &byte, 1);
}
uint16_t uart_crc16_ccitt(const uint8_t *data, size_t length, uint16_t init_val) {
    return (uint16_t)embed_crc_update(EMBED_CRC16_CCITT, init_val, data, length);
}
