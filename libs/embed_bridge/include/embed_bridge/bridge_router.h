#pragma once
#ifndef BRIDGE_ROUTER_H
#define BRIDGE_ROUTER_H

#include "bridge_types.h"
#include "embed_opcua/opcua_server.h"
#include "embed_eip/eip_adapter.h"
#include "embed_canopen/canopen_node.h"
#include "embed_mqtt/mqtt_client.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bridge_rule_t rules[BRIDGE_MAX_RULES];
    size_t rule_count;

    /* バインドされたプロトコルインスタンス */
    uint16_t *modbus_holding_regs;
    size_t modbus_reg_count;

    opcua_server_t *opcua_server;
    eip_adapter_t *eip_adapter;
    canopen_node_t *canopen_node;
    mqtt_client_t *mqtt_client;
} bridge_router_t;

/**
 * @brief ルーターインスタンスを初期化します。
 */
void bridge_router_init(bridge_router_t *router);

/**
 * @brief Modbus 保持レジスタ配列をバインドします。
 */
void bridge_router_bind_modbus(bridge_router_t *router, uint16_t *holding_regs, size_t reg_count);

/**
 * @brief OPC-UA サーバインスタンスをバインドします。
 */
void bridge_router_bind_opcua(bridge_router_t *router, opcua_server_t *opcua_server);

/**
 * @brief EtherNet/IP アダプタインスタンスをバインドします。
 */
void bridge_router_bind_eip(bridge_router_t *router, eip_adapter_t *eip_adapter);

/**
 * @brief CANopen ノードインスタンスをバインドします。
 */
void bridge_router_bind_canopen(bridge_router_t *router, canopen_node_t *canopen_node);

/**
 * @brief MQTT クライアントインスタンスをバインドします。
 */
void bridge_router_bind_mqtt(bridge_router_t *router, mqtt_client_t *mqtt_client);

/**
 * @brief マッピングルールを追加登録します。
 * @return 登録されたルールのインデックス (失敗時は -1)
 */
int bridge_router_add_rule(bridge_router_t *router, const bridge_rule_t *rule);

/**
 * @brief 全てのアクティブなルールに対して即時同期転送を実行します。
 * @return 転送が実行されたルール数
 */
size_t bridge_router_sync_all(bridge_router_t *router);

/**
 * @brief 周期タイマーを進め、周期設定・変化検知に応じた同期転送を実行します。
 * @param router ルーターインスタンス
 * @param elapsed_ms 経過時間 [ms]
 * @return 転送が実行されたルール数
 */
size_t bridge_router_tick(bridge_router_t *router, uint32_t elapsed_ms);

#ifdef __cplusplus
}
#endif

#endif /* BRIDGE_ROUTER_H */
