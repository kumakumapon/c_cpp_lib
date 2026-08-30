#pragma once
#ifndef MODBUS_SLAVE_H
#define MODBUS_SLAVE_H

#include "modbus_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief スレーブレジスタコールバック型定義
 */
typedef modbus_exception_t (*modbus_read_coils_cb)(uint16_t addr, uint16_t count, uint8_t *dest_bytes, void *user_data);
typedef modbus_exception_t (*modbus_read_discrete_cb)(uint16_t addr, uint16_t count, uint8_t *dest_bytes, void *user_data);
typedef modbus_exception_t (*modbus_read_holding_cb)(uint16_t addr, uint16_t count, uint16_t *dest_regs, void *user_data);
typedef modbus_exception_t (*modbus_read_input_cb)(uint16_t addr, uint16_t count, uint16_t *dest_regs, void *user_data);

typedef modbus_exception_t (*modbus_write_coils_cb)(uint16_t addr, uint16_t count, const uint8_t *src_bytes, void *user_data);
typedef modbus_exception_t (*modbus_write_holding_cb)(uint16_t addr, uint16_t count, const uint16_t *src_regs, void *user_data);

/* スレーブコールバックテーブル */
typedef struct {
    modbus_read_coils_cb       read_coils;
    modbus_read_discrete_cb    read_discrete_inputs;
    modbus_read_holding_cb     read_holding_registers;
    modbus_read_input_cb       read_input_registers;

    modbus_write_coils_cb      write_coils;
    modbus_write_holding_cb    write_holding_registers;
} modbus_slave_callbacks_t;

/* スレーブ管理構造体 */
typedef struct {
    uint8_t unit_id;                            /**< スレーブアドレス (1〜247) */
    modbus_slave_callbacks_t callbacks;        /**< ユーザーコールバック */
    void *user_data;
} modbus_slave_t;

/**
 * @brief スレーブインスタンスを初期化します。
 */
void modbus_slave_init(modbus_slave_t *slave, uint8_t unit_id,
                       const modbus_slave_callbacks_t *callbacks, void *user_data);

/**
 * @brief 受信したModbus PDUを処理し、応答PDUを生成します。
 * 
 * @param slave スレーブインスタンス
 * @param req_pdu 受信PDU (ファンクションコード以降)
 * @param req_len 受信PDU長
 * @param resp_pdu 応答PDUを格納するバッファ
 * @param resp_len 応答PDU長が格納されるポインタ
 * @return true 応答を送信すべき, false ブロードキャスト時など応答不要
 */
bool modbus_slave_process_pdu(const modbus_slave_t *slave,
                              const uint8_t *req_pdu, size_t req_len,
                              uint8_t *resp_pdu, size_t *resp_len);

#ifdef __cplusplus
}
#endif

#endif /* MODBUS_SLAVE_H */
