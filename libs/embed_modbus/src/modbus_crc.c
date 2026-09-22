#include "embed_modbus/modbus_crc.h"
#include "embed_crc/embed_crc.h"
uint16_t modbus_crc16(const uint8_t *data, size_t length) { return !data || !length ? 0 : (uint16_t)embed_crc(EMBED_CRC16_MODBUS, data, length); }
uint8_t modbus_lrc(const uint8_t *buffer, size_t length) {
    if (!buffer || length == 0) return 0;

    uint8_t lrc = 0;
    for (size_t i = 0; i < length; ++i) {
        lrc += buffer[i];
    }
    return (uint8_t)(-((int8_t)lrc));
}
