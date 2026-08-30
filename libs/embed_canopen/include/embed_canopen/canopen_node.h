#pragma once
#ifndef CANOPEN_NODE_H
#define CANOPEN_NODE_H

#include "canopen_types.h"
#include "canopen_od.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t node_id;
    canopen_nmt_state_t nmt_state;
    canopen_od_t od;

    /* 標準通信パラメータ */
    uint32_t device_type;           /**< 0x1000: Device Type */
    uint8_t  error_register;        /**< 0x1001: Error Register */
    uint32_t cob_id_sync;           /**< 0x1005: COB-ID SYNC */
    char     device_name[32];       /**< 0x1008: Manufacturer Device Name */
    uint32_t vendor_id;             /**< 0x1018.1: Vendor ID */
    uint32_t product_code;          /**< 0x1018.2: Product Code */
    uint32_t revision_number;       /**< 0x1018.3: Revision Number */
    uint32_t serial_number;         /**< 0x1018.4: Serial Number */
    uint16_t heartbeat_producer_ms; /**< 0x1017: Producer Heartbeat Time [ms] */

    uint32_t heartbeat_timer_ms;
} canopen_node_t;

/**
 * @brief CANopen ノードを初期化し、必須の標準通信ODエントリ (0x1000〜0x1018) を自動構成します。
 * 
 * @param node ノード構造体
 * @param node_id ノードID (1〜127)
 * @param device_name デバイス名
 */
void canopen_node_init(canopen_node_t *node, uint8_t node_id, const char *device_name);

/**
 * @brief 受信したCANフレームを処理し、必要に応じて応答フレームを生成します。
 * 
 * @param node ノード構造体
 * @param rx_frame 受信CANフレーム
 * @param tx_frame 応答CANフレーム格納先
 * @return true 応答フレームの送信が必要 (tx_frame 有効)
 */
bool canopen_node_process_frame(canopen_node_t *node, const can_frame_t *rx_frame, can_frame_t *tx_frame);

/**
 * @brief タイマーを進行させ、Heartbeat 等の周期メッセージを生成します。
 * 
 * @param node ノード構造体
 * @param elapsed_ms 経過時間 [ms]
 * @param tx_frame 送信フレーム格納先
 * @return true 送信フレームが発生した場合
 */
bool canopen_node_timer_tick(canopen_node_t *node, uint32_t elapsed_ms, can_frame_t *tx_frame);

/**
 * @brief TPDO1 (0x180 + NodeID) フレームを生成します。
 * 
 * @param node ノード構造体
 * @param data 送信データ (最大8バイト)
 * @param len データ長
 * @param tx_frame 出力CANフレーム
 * @return true 生成成功
 */
bool canopen_node_send_tpdo1(const canopen_node_t *node, const uint8_t *data, uint8_t len, can_frame_t *tx_frame);

#ifdef __cplusplus
}
#endif

#endif /* CANOPEN_NODE_H */
