#pragma once
#ifndef CANOPEN_TYPES_H
#define CANOPEN_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 容量 (コンパイル時に変更可。ライブラリと利用側で同じ値にすること) ---- */
#ifndef CANOPEN_MAX_OD_ENTRIES
#define CANOPEN_MAX_OD_ENTRIES      64      /**< OD エントリ数 (通信プロファイルの 9 件を含む) */
#endif
#ifndef CANOPEN_SDO_BUFFER_SIZE
#define CANOPEN_SDO_BUFFER_SIZE     128     /**< SDO 分割転送で扱える最大オブジェクト長 */
#endif
#define CANOPEN_MAX_CAN_DATA_LEN    8
#define CANOPEN_MAX_TPDO            4
#define CANOPEN_MAX_RPDO            4
#define CANOPEN_PDO_MAX_MAP         8       /**< 1 PDO に割り当てられるオブジェクト数 */
#define CANOPEN_TX_QUEUE_SIZE       16      /**< 送信待ちフレーム数 */
#define CANOPEN_MAX_HB_CONSUMERS    4       /**< 0x1016 で監視できるノード数 */
#define CANOPEN_ERROR_HISTORY       4       /**< 0x1003 に保持するエラー数 */

/**
 * @brief CAN 2.0A / 2.0B メッセージフレーム構造体
 */
typedef struct {
    uint32_t id;                        /**< CAN ID (11-bit Standard / 29-bit Extended) */
    uint8_t  len;                       /**< データ長 (DLC: 0〜8) */
    uint8_t  data[CANOPEN_MAX_CAN_DATA_LEN]; /**< ペイロード */
    bool     is_extended;               /**< 拡張IDフラグ (CANopen は 11bit のみ扱い、拡張フレームは無視) */
    bool     is_rtr;                    /**< リモートフレームフラグ (無視) */
} can_frame_t;

/**
 * @brief CANopen COB-ID ファンクションコード (CiA 301 事前定義接続セット)
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
    CANOPEN_FC_TPDO3            = 0x380, /**< TPDO3 (0x380 + NodeID) */
    CANOPEN_FC_RPDO3            = 0x400, /**< RPDO3 (0x400 + NodeID) */
    CANOPEN_FC_TPDO4            = 0x480, /**< TPDO4 (0x480 + NodeID) */
    CANOPEN_FC_RPDO4            = 0x500, /**< RPDO4 (0x500 + NodeID) */
    CANOPEN_FC_TSDO             = 0x580, /**< SDO Tx (Slave->Master: 0x580 + NodeID) */
    CANOPEN_FC_RSDO             = 0x600, /**< SDO Rx (Master->Slave: 0x600 + NodeID) */
    CANOPEN_FC_HEARTBEAT        = 0x700  /**< NMT Error Control / Heartbeat (0x700 + NodeID) */
} canopen_fc_t;

/**
 * @brief NMT 状態 (Network Management States)
 */
typedef enum {
    CANOPEN_NMT_STATE_INITIALISING     = 0x00, /**< 初期化中 (ブートアップメッセージの値) */
    CANOPEN_NMT_STATE_STOPPED          = 0x04, /**< 停止 (NMT と Heartbeat のみ) */
    CANOPEN_NMT_STATE_OPERATIONAL      = 0x05, /**< 運用中 (PDO送受信可能) */
    CANOPEN_NMT_STATE_PRE_OPERATIONAL  = 0x7F  /**< 準備完了 (SDO・EMCY・SYNC 可、PDO 不可) */
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
 * @brief オブジェクトディクショナリ (OD) データ型 (CiA 301 のデータ型インデックス)
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
    OD_TYPE_VISIBLE_STRING = 0x09,
    OD_TYPE_OCTET_STRING = 0x0A,
    OD_TYPE_DOMAIN      = 0x0F,
    OD_TYPE_REAL64      = 0x11,
    OD_TYPE_INT64       = 0x15,
    OD_TYPE_UINT64      = 0x1B
} canopen_od_type_t;

/**
 * @brief オブジェクトディクショナリ アクセス権限
 */
typedef enum {
    OD_ACCESS_RO        = 0x01, /**< Read Only */
    OD_ACCESS_WO        = 0x02, /**< Write Only */
    OD_ACCESS_RW        = 0x03  /**< Read / Write */
} canopen_od_access_t;

struct canopen_od_entry;
/**
 * @brief 書き込み前の検証フック。
 * @param data 書き込もうとしている値 (ホストのバイト順)
 * @return 0 なら受け入れ (値がコピーされる)。それ以外は SDO アボートコードで拒否。
 */
typedef uint32_t (*canopen_od_write_hook)(const struct canopen_od_entry *entry, const void *data,
                                          size_t len, void *user);

/**
 * @brief オブジェクトディクショナリエントリ構造体
 */
typedef struct canopen_od_entry {
    uint16_t index;             /**< インデックス (例: 0x1000, 0x2000) */
    uint8_t  subindex;          /**< サブインデックス (0x00〜) */
    canopen_od_type_t type;     /**< データ型 */
    canopen_od_access_t access; /**< アクセス権限 */
    void     *data_ptr;         /**< 実データへのポインタ (ホストのバイト順) */
    size_t   data_len;          /**< データバイト長 (文字列型は格納領域の長さ) */
    canopen_od_write_hook on_write; /**< 書き込み検証フック (NULL 可) */
    void     *hook_user;
} canopen_od_entry_t;

/* ---- SDO アボートコード (CiA 301 Table 22) ---- */
#define CANOPEN_SDO_ABORT_TOGGLE          0x05030000u /**< トグルビットが交互でない */
#define CANOPEN_SDO_ABORT_TIMEOUT         0x05040000u /**< SDO プロトコルのタイムアウト */
#define CANOPEN_SDO_ABORT_CS              0x05040001u /**< 不正・未知のコマンド指定子 */
#define CANOPEN_SDO_ABORT_OUT_OF_MEMORY   0x05040005u /**< メモリ不足 */
#define CANOPEN_SDO_ABORT_UNSUPPORTED     0x06010000u /**< サポートされないアクセス */
#define CANOPEN_SDO_ABORT_WRITE_ONLY      0x06010001u /**< 書き込み専用オブジェクトの読み出し */
#define CANOPEN_SDO_ABORT_READ_ONLY       0x06010002u /**< 読み出し専用オブジェクトへの書き込み */
#define CANOPEN_SDO_ABORT_NO_OBJECT       0x06020000u /**< オブジェクトが存在しない */
#define CANOPEN_SDO_ABORT_NOT_MAPPABLE    0x06040041u /**< PDO にマップできない */
#define CANOPEN_SDO_ABORT_PDO_LENGTH      0x06040042u /**< マップ長が PDO 長を超える */
#define CANOPEN_SDO_ABORT_PARAM_INCOMPAT  0x06040043u /**< パラメータの不整合 */
#define CANOPEN_SDO_ABORT_TYPE_MISMATCH   0x06070010u /**< データ型・長さの不一致 */
#define CANOPEN_SDO_ABORT_LENGTH_HIGH     0x06070012u /**< 長さが長すぎる */
#define CANOPEN_SDO_ABORT_LENGTH_LOW      0x06070013u /**< 長さが短すぎる */
#define CANOPEN_SDO_ABORT_NO_SUBINDEX     0x06090011u /**< サブインデックスが存在しない */
#define CANOPEN_SDO_ABORT_VALUE_RANGE     0x06090030u /**< 値の範囲外 */
#define CANOPEN_SDO_ABORT_VALUE_HIGH      0x06090031u /**< 値が大きすぎる */
#define CANOPEN_SDO_ABORT_VALUE_LOW       0x06090032u /**< 値が小さすぎる */
#define CANOPEN_SDO_ABORT_GENERAL         0x08000000u /**< 一般エラー */
#define CANOPEN_SDO_ABORT_DEVICE_STATE    0x08000022u /**< 現在の装置状態では転送できない */
#define CANOPEN_SDO_ABORT_NO_DATA         0x08000024u /**< データがない */

/* ---- EMCY エラーコード (CiA 301 Table 21 の一部) ---- */
#define CANOPEN_EMCY_NO_ERROR             0x0000u /**< エラーリセット / エラーなし */
#define CANOPEN_EMCY_GENERIC              0x1000u
#define CANOPEN_EMCY_CURRENT              0x2000u
#define CANOPEN_EMCY_VOLTAGE              0x3000u
#define CANOPEN_EMCY_TEMPERATURE          0x4000u
#define CANOPEN_EMCY_HARDWARE             0x5000u
#define CANOPEN_EMCY_SOFTWARE             0x6000u
#define CANOPEN_EMCY_COMMUNICATION        0x8100u
#define CANOPEN_EMCY_CAN_OVERRUN          0x8110u
#define CANOPEN_EMCY_HEARTBEAT            0x8130u /**< Life guard / Heartbeat エラー */
#define CANOPEN_EMCY_PDO_LENGTH           0x8210u /**< PDO 長が不足して処理できない */

/* ---- エラーレジスタ 0x1001 のビット ---- */
#define CANOPEN_ERR_REG_GENERIC           0x01u
#define CANOPEN_ERR_REG_CURRENT           0x02u
#define CANOPEN_ERR_REG_VOLTAGE           0x04u
#define CANOPEN_ERR_REG_TEMPERATURE       0x08u
#define CANOPEN_ERR_REG_COMMUNICATION     0x10u
#define CANOPEN_ERR_REG_PROFILE           0x20u
#define CANOPEN_ERR_REG_MANUFACTURER      0x80u

#ifdef __cplusplus
}
#endif

#endif /* CANOPEN_TYPES_H */
