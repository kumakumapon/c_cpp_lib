#pragma once
#ifndef OPCUA_SERVER_H
#define OPCUA_SERVER_H

#include "opcua_types.h"
#include "opcua_binary.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    opcua_nodeid_t node_id;
    opcua_node_class_t node_class;
    opcua_qualified_name_t browse_name;
    opcua_localized_text_t display_name;
    opcua_datavalue_t value;
    uint8_t access_level; // 0x01: Read, 0x02: Write, 0x03: Read/Write
    bool is_used;
} opcua_node_t;

typedef struct {
    opcua_node_t nodes[OPCUA_MAX_NODES];
    size_t node_count;
    char endpoint_url[OPCUA_MAX_STRING_LEN];
} opcua_server_t;

/**
 * @brief OPC-UA サーバインスタンスを初期化します。
 */
void opcua_server_init(opcua_server_t *server, const char *endpoint_url);

/**
 * @brief 変数ノード (Variable Node) を追加します。
 */
bool opcua_server_add_variable(opcua_server_t *server,
                               const opcua_nodeid_t *node_id,
                               const char *browse_name,
                               const opcua_variant_t *initial_value,
                               uint8_t access_level);

/**
 * @brief ノードの値を直接更新します (センサ値の更新など)。
 */
bool opcua_server_write_value(opcua_server_t *server,
                              const opcua_nodeid_t *node_id,
                              const opcua_variant_t *value);

/**
 * @brief ノードの値を読み出します。
 */
bool opcua_server_read_value(const opcua_server_t *server,
                             const opcua_nodeid_t *node_id,
                             opcua_datavalue_t *dest_value);

/**
 * @brief Read サービス要求を処理し、応答を生成します。
 */
uint32_t opcua_server_process_read(const opcua_server_t *server,
                                   const opcua_nodeid_t *node_id,
                                   opcua_attribute_id_t attr_id,
                                   opcua_datavalue_t *out_value);

/**
 * @brief Write サービス要求を処理し、ノード値を更新します。
 */
uint32_t opcua_server_process_write(opcua_server_t *server,
                                    const opcua_nodeid_t *node_id,
                                    opcua_attribute_id_t attr_id,
                                    const opcua_datavalue_t *in_value);

/**
 * @brief 受信したTCPトランスポートメッセージ (HEL, MSG等) を処理し、応答メッセージを生成します。
 */
size_t opcua_server_process_tcp_message(opcua_server_t *server,
                                        const uint8_t *req_data, size_t req_len,
                                        uint8_t *resp_data, size_t max_resp_len);

#ifdef __cplusplus
}
#endif

#endif /* OPCUA_SERVER_H */
