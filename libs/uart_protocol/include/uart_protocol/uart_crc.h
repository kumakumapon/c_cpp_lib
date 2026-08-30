#pragma once
#ifndef UART_CRC_H
#define UART_CRC_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief CRC-16-CCITT (多項式: 0x1021, 初期値: 0xFFFF) を計算します。
 * 
 * @param data 計算対象データの先頭ポインタ
 * @param length データ長 (バイト数)
 * @param init_val 初期CRC値 (初回は0xFFFF、継続計算時は前回の戻り値)
 * @return uint16_t 計算されたCRC16値
 */
uint16_t uart_crc16_ccitt(const uint8_t *data, size_t length, uint16_t init_val);

/**
 * @brief 単一バイトに対するCRC-16の更新を行います。
 */
uint16_t uart_crc16_update(uint16_t crc, uint8_t byte);

#ifdef __cplusplus
}
#endif

#endif /* UART_CRC_H */
