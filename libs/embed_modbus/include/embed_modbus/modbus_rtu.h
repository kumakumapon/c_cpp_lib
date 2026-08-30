#pragma once
#ifndef MODBUS_RTU_H
#define MODBUS_RTU_H

#include "modbus_types.h"
#include "modbus_slave.h"
#include "modbus_master.h"
#include "modbus_crc.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Modbus RTU フレームをエンコードします (Unit ID + PDU + CRC16)。
 */
bool modbus_rtu_encode_frame(uint8_t unit_id, const uint8_t *pdu, size_t pdu_len,
                             uint8_t *adu, size_t *adu_len);

/**
 * @brief Modbus RTU フレームを検証・デコードします (CRC検証 + Unit ID / PDU 抽出)。
 */
modbus_status_t modbus_rtu_decode_frame(const uint8_t *adu, size_t adu_len,
                                        uint8_t *unit_id, const uint8_t **pdu, size_t *pdu_len);

/**
 * @brief スレーブが受信したRTUリクエストフレームを直接処理し、応答RTUフレームを作成します。
 * 
 * @param slave スレーブインスタンス
 * @param req_adu 受信RTUフレーム
 * @param req_len 受信長
 * @param resp_adu 応答RTUフレーム格納バッファ
 * @param resp_len 応答長格納ポインタ
 * @return true 応答送信が必要, false 宛先不一致またはブロードキャストで応答不要
 */
bool modbus_rtu_slave_handle(const modbus_slave_t *slave,
                             const uint8_t *req_adu, size_t req_len,
                             uint8_t *resp_adu, size_t *resp_len);

#ifdef __cplusplus
}
#endif

#endif /* MODBUS_RTU_H */
