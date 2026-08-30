#pragma once
#ifndef CANOPEN_TYPES_H
#define CANOPEN_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CANOPEN_MAX_OD_ENTRIES      64
#define CANOPEN_MAX_CAN_DATA_LEN    8

/**
 * @brief CAN 2.0A / 2.0B メッセージフレーム構造体
 */
typedef struct {
    uint32_t id;                        /**< CAN ID (11-bit Standard / 29-bit Extended) */
    uint8_t  len;                       /**< データ長 (DLC: 0〜8) */
    uint8_t  data[CANOPEN_MAX_CAN_DATA_LEN]; /**< ペイロード */
    bool     is_extended;               /**< 拡張IDフラグ */
    bool     is_rtr;                    /**< リモートフレームフラグ */
} can_frame_t;

/**
 * @brief CANopen COB-ID ファンクションコード (CiA 301)
 */
typedef enum {
    CANOPEN_FC_NMT_SERVICE      = 0x000, /**< NMT コマンド (ID=0) */
    CANOPEN_FC_SYNC             = 0x080, /**< SYNC (ID=0x80) */
    CANOPEN_FC_EMERGENCY        = 0x080, /**< EMCY (0x80 + NodeID) */
    CANOPEN_FC_TIME_STAMP       = 0x100, /**< TIME (ID=0x100) */
    CANOPEN_FC_TPDO1            = 0x180, /**< TPDO1 (0x180 + NodeID) */
    CANOPEN_FC_RPDO1            = 0x200, /**< RPDO1 (0x200 + NodeID) */
    CANOPEN_FC_TPDO2            = 0x280, /**< TPDO2 (0x280 + NodeID) */
    CANOPEN_FC_RPDO2            = 0x300, /**< RPDO2 (0x300 + NodeID) */
    CANOPEN_FC_TSDO             = 0x580, /**< SDO Tx (Slave->Master: 0x580 + NodeID) */
    CANOPEN_FC_RSDO             = 0x600, /**< SDO Rx (Master->Slave: 0x600 + NodeID) */
    CANOPEN_FC_HEARTBEAT        = 0x700  /**< NMT Error Control / Heartbeat (0x700 + NodeID) */
} canopen_fc_t;

/**
 * @brief NMT 状態 (Network Management States)
 */
typedef enum {
    CANOPEN_NMT_STATE_INITIALISING     = 0x00, /**< 初期化中 */
    CANOPEN_NMT_STATE_STOPPED          = 0x04, /**< 停止 */
    CANOPEN_NMT_STATE_OPERATIONAL      = 0x05, /**< 運用中 (PDO送受信可能) */
    CANOPEN_NMT_STATE_PRE_OPERATIONAL  = 0x7F  /**< 準備完了 (SDOのみ可能) */
} canopen_nmt_state_t;

/**
 * @brief NMT コマンド指定子 (Command Specifiers)
 */
typedef enum {
    CANOPEN_NMT_CMD_START              = 0x01, /**< Start Remote Node (-> Operational) */
    CANOPEN_NMT_CMD_STOP               = 0x02, /**< Stop Remote Node (-> Stopped) */
    CANOPEN_NMT_CMD_ENTER_PRE_OP       = 0x80, /**< Enter Pre-Operational */
    CANOPEN_NMT_CMD_RESET_NODE         = 0x81, /**< Reset Node */
    CANOPEN_NMT_CMD_RESET_COMM         = 0x82  /**< Reset Communication */
} canopen_nmt_cmd_t;

/**
 * @brief オブジェクトディクショナリ (OD) データ型
 */
typedef enum {
    OD_TYPE_BOOLEAN     = 0x01,
    OD_TYPE_INT8        = 0x02,
    OD_TYPE_INT16       = 0x03,
    OD_TYPE_INT32       = 0x04,
    OD_TYPE_UINT8       = 0x05,
    OD_TYPE_UINT16      = 0x06,
    OD_TYPE_UINT32      = 0x07,
    OD_TYPE_REAL32      = 0x08,
    OD_TYPE_VISIBLE_STRING = 0x09
} canopen_od_type_t;

/**
 * @brief オブジェクトディクショナリ アクセス権限
 */
typedef enum {
    OD_ACCESS_RO        = 0x01, /**< Read Only */
    OD_ACCESS_WO        = 0x02, /**< Write Only */
    OD_ACCESS_RW        = 0x03  /**< Read / Write */
} canopen_od_access_t;

/**
 * @brief オブジェクトディクショナリエントリ構造体
 */
typedef struct {
    uint16_t index;             /**< インデックス (例: 0x1000, 0x2000) */
    uint8_t  subindex;          /**< サブインデックス (0x00〜) */
    canopen_od_type_t type;     /**< データ型 */
    canopen_od_access_t access; /**< アクセス権限 */
    void     *data_ptr;         /**< 実データへのポインタ */
    size_t   data_len;          /**< データバイト長 */
} canopen_od_entry_t;

#ifdef __cplusplus
}
#endif

#endif /* CANOPEN_TYPES_H */
