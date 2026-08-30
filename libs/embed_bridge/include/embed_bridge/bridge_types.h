#pragma once
#ifndef BRIDGE_TYPES_H
#define BRIDGE_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "embed_opcua/opcua_types.h"
#include "embed_canopen/canopen_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BRIDGE_MAX_RULES            32
#define BRIDGE_MAX_TOPIC_LEN        64
#define BRIDGE_MAX_DATA_SIZE        16

/**
 * @brief プロトコル種別
 */
typedef enum {
    BRIDGE_PROTO_RAW_VAR        = 0, /**< 直接メモリアドレス / 変数 */
    BRIDGE_PROTO_MODBUS_REG     = 1, /**< Modbus 保持レジスタ (40001〜) */
    BRIDGE_PROTO_OPCUA_NODE     = 2, /**< OPC-UA 変数ノード */
    BRIDGE_PROTO_EIP_ASSEMBLY   = 3, /**< EtherNet/IP アセンブリインスタンス + オフセット */
    BRIDGE_PROTO_MQTT_TOPIC     = 4, /**< MQTT パブリッシュトピック */
    BRIDGE_PROTO_CANOPEN_OD     = 5  /**< CANopen オブジェクトディクショナリ (Index/SubIndex) */
} bridge_proto_type_t;

/**
 * @brief 転送データ型
 */
typedef enum {
    BRIDGE_TYPE_BOOL            = 0,
    BRIDGE_TYPE_INT16           = 1,
    BRIDGE_TYPE_UINT16          = 2,
    BRIDGE_TYPE_INT32           = 3,
    BRIDGE_TYPE_UINT32          = 4,
    BRIDGE_TYPE_FLOAT           = 5,
    BRIDGE_TYPE_RAW_BYTES       = 6
} bridge_data_type_t;

/**
 * @brief 同期トリガー方式
 */
typedef enum {
    BRIDGE_SYNC_ALWAYS          = 0, /**< 毎サイクル無条件転送 */
    BRIDGE_SYNC_ON_CHANGE       = 1, /**< 値変化時のみ転送 (COV: Change of Value) */
    BRIDGE_SYNC_PERIODIC        = 2  /**< 一定周期 (interval_ms) ごとに転送 */
} bridge_sync_mode_t;

/**
 * @brief エンドポイント指定構造体
 */
typedef struct {
    bridge_proto_type_t proto;
    union {
        void *raw_ptr;
        uint16_t modbus_addr;
        opcua_nodeid_t opcua_node;
        struct {
            uint16_t instance_id;
            uint16_t offset;
        } eip;
        char mqtt_topic[BRIDGE_MAX_TOPIC_LEN];
        struct {
            uint16_t index;
            uint8_t subindex;
        } canopen;
    } target;
} bridge_endpoint_t;

/**
 * @brief ルーティング・マッピングルール定義構造体
 */
typedef struct {
    uint16_t rule_id;
    bridge_endpoint_t src;
    bridge_endpoint_t dst;
    bridge_data_type_t src_type;
    bridge_data_type_t dst_type;
    size_t data_size;
    float scale;
    float offset;
    bridge_sync_mode_t sync_mode;
    uint32_t interval_ms;
    uint32_t timer_ms;
    uint8_t last_data[BRIDGE_MAX_DATA_SIZE];
    bool has_last_data;
    bool is_active;
} bridge_rule_t;

#ifdef __cplusplus
}
#endif

#endif /* BRIDGE_TYPES_H */
