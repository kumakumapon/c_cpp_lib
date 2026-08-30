#pragma once
#ifndef MODBUS_CRC_H
#define MODBUS_CRC_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Modbus RTU CRC-16 (多項式: 0xA001, 初期値: 0xFFFF) を計算します。
 * 
 * @param buffer データバッファ
 * @param length バイト長
 * @return uint16_t 計算されたCRC16値 (下位バイト・上位バイトの順でフレーム末尾に付加)
 */
uint16_t modbus_crc16(const uint8_t *buffer, size_t length);

/**
 * @brief Modbus ASCII 用 LRC (Longitudinal Redundancy Check) を計算します。
 */
uint8_t modbus_lrc(const uint8_t *buffer, size_t length);

#ifdef __cplusplus
}
#endif

#endif /* MODBUS_CRC_H */
