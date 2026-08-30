#pragma once
#ifndef MODBUS_TYPES_H
#define MODBUS_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------
 * Modbus 定数定義
 * --------------------------------------------------------- */
#define MODBUS_BROADCAST_ADDRESS    0       /**< ブロードキャストアドレス */
#define MODBUS_MAX_ADU_LENGTH       256     /**< Modbus RTU ADU最大長 (バイト) */
#define MODBUS_MAX_PDU_LENGTH       253     /**< Modbus PDU最大長 (バイト) */

#define MODBUS_MAX_READ_COILS       2000    /**< 1回のRead Coils最大数 */
#define MODBUS_MAX_READ_REGISTERS   125     /**< 1回のRead Registers最大数 */
#define MODBUS_MAX_WRITE_COILS      1968    /**< 1回のWrite Multiple Coils最大数 */
#define MODBUS_MAX_WRITE_REGISTERS  123     /**< 1回のWrite Multiple Registers最大数 */

/* ---------------------------------------------------------
 * ファンクションコード (Function Codes)
 * --------------------------------------------------------- */
typedef enum {
    MODBUS_FC_READ_COILS                = 0x01, /**< コイル読み出し (0x) */
    MODBUS_FC_READ_DISCRETE_INPUTS      = 0x02, /**< 離散入力読み出し (1x) */
    MODBUS_FC_READ_HOLDING_REGISTERS    = 0x03, /**< 保持レジスタ読み出し (4x) */
    MODBUS_FC_READ_INPUT_REGISTERS      = 0x04, /**< 入力レジスタ読み出し (3x) */
    MODBUS_FC_WRITE_SINGLE_COIL         = 0x05, /**< 単一コイル書き込み */
    MODBUS_FC_WRITE_SINGLE_REGISTER     = 0x06, /**< 単一保持レジスタ書き込み */
    MODBUS_FC_WRITE_MULTIPLE_COILS      = 0x0F, /**< 複数コイル書き込み */
    MODBUS_FC_WRITE_MULTIPLE_REGISTERS  = 0x10, /**< 複数保持レジスタ書き込み */
} modbus_function_code_t;

/* ---------------------------------------------------------
 * 例外コード (Exception Codes)
 * --------------------------------------------------------- */
typedef enum {
    MODBUS_EX_NONE                      = 0x00, /**< 正常終了 */
    MODBUS_EX_ILLEGAL_FUNCTION          = 0x01, /**< 未サポートファンクション */
    MODBUS_EX_ILLEGAL_DATA_ADDRESS      = 0x02, /**< アドレス範囲外 */
    MODBUS_EX_ILLEGAL_DATA_VALUE        = 0x03, /**< データ値不正 */
    MODBUS_EX_SERVER_DEVICE_FAILURE     = 0x04, /**< スレーブ内部エラー */
    MODBUS_EX_ACKNOWLEDGE               = 0x05, /**< 処理受付 (時間要) */
    MODBUS_EX_SERVER_DEVICE_BUSY        = 0x06, /**< スレーブビジー */
    MODBUS_EX_MEMORY_PARITY_ERROR       = 0x08, /**< パリティエラー */
    MODBUS_EX_GATEWAY_PATH_UNAVAILABLE  = 0x0A, /**< ゲートウェイパス異常 */
    MODBUS_EX_GATEWAY_TARGET_FAILED     = 0x0B  /**< ターゲット応答なし */
} modbus_exception_t;

/* ---------------------------------------------------------
 * エラーコード
 * --------------------------------------------------------- */
typedef enum {
    MODBUS_OK                           = 0,
    MODBUS_ERR_INVALID_PARAM            = -1,
    MODBUS_ERR_CRC                      = -2,
    MODBUS_ERR_TIMEOUT                  = -3,
    MODBUS_ERR_BUFFER_OVERFLOW          = -4,
    MODBUS_ERR_EXCEPTION_RECEIVED       = -5,
    MODBUS_ERR_INVALID_RESPONSE         = -6,
    MODBUS_ERR_ADDRESS_MISMATCH         = -7,
    MODBUS_ERR_FUNCTION_MISMATCH        = -8
} modbus_status_t;

#ifdef __cplusplus
}
#endif

#endif /* MODBUS_TYPES_H */
