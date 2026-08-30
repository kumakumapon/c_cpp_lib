#pragma once
#ifndef OPCUA_TYPES_H
#define OPCUA_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------
 * OPC-UA 定数 & StatusCode 定義
 * --------------------------------------------------------- */
#define OPCUA_STATUS_GOOD                          0x00000000U
#define OPCUA_STATUS_BAD_UNEXPECTED_ERROR          0x80010000U
#define OPCUA_STATUS_BAD_NODE_ID_UNKNOWN           0x80340000U
#define OPCUA_STATUS_BAD_ATTRIBUTE_ID_INVALID      0x80350000U
#define OPCUA_STATUS_BAD_NOT_WRITABLE              0x803B0000U
#define OPCUA_STATUS_BAD_TYPE_MISMATCH             0x80740000U
#define OPCUA_STATUS_BAD_NOT_READABLE              0x803A0000U

#define OPCUA_MAX_STRING_LEN    64
#define OPCUA_MAX_NODES         32
#define OPCUA_MAX_MESSAGE_SIZE  1024

/* ---------------------------------------------------------
 * 組み込みデータ型 (Built-in Data Types)
 * --------------------------------------------------------- */
typedef enum {
    OPCUA_TYPE_NULL         = 0,
    OPCUA_TYPE_BOOLEAN      = 1,
    OPCUA_TYPE_SBYTE        = 2,
    OPCUA_TYPE_BYTE         = 3,
    OPCUA_TYPE_INT16        = 4,
    OPCUA_TYPE_UINT16       = 5,
    OPCUA_TYPE_INT32        = 6,
    OPCUA_TYPE_UINT32       = 7,
    OPCUA_TYPE_INT64        = 8,
    OPCUA_TYPE_UINT64       = 9,
    OPCUA_TYPE_FLOAT        = 10,
    OPCUA_TYPE_DOUBLE       = 11,
    OPCUA_TYPE_STRING       = 12,
    OPCUA_TYPE_DATETIME     = 13,
    OPCUA_TYPE_STATUSCODE   = 19
} opcua_type_id_t;

/* ---------------------------------------------------------
 * アトリビュートID (Attribute IDs)
 * --------------------------------------------------------- */
typedef enum {
    OPCUA_ATTR_NODE_ID          = 1,
    OPCUA_ATTR_NODE_CLASS       = 2,
    OPCUA_ATTR_BROWSE_NAME      = 3,
    OPCUA_ATTR_DISPLAY_NAME     = 4,
    OPCUA_ATTR_DESCRIPTION      = 5,
    OPCUA_ATTR_VALUE            = 13,
    OPCUA_ATTR_DATA_TYPE        = 14,
    OPCUA_ATTR_VALUE_RANK       = 15,
    OPCUA_ATTR_ACCESS_LEVEL     = 17,
} opcua_attribute_id_t;

/* ---------------------------------------------------------
 * ノードクラス (Node Classes)
 * --------------------------------------------------------- */
typedef enum {
    OPCUA_NODECLASS_UNSPECIFIED = 0,
    OPCUA_NODECLASS_OBJECT      = 1,
    OPCUA_NODECLASS_VARIABLE    = 2,
    OPCUA_NODECLASS_METHOD      = 4,
    OPCUA_NODECLASS_OBJECT_TYPE = 8,
    OPCUA_NODECLASS_VARIABLE_TYPE = 16,
    OPCUA_NODECLASS_DATA_TYPE   = 64
} opcua_node_class_t;

/* ---------------------------------------------------------
 * 基本構造体定義
 * --------------------------------------------------------- */

/* NodeId (数値型または文字列型) */
typedef enum {
    OPCUA_NODEID_NUMERIC = 0,
    OPCUA_NODEID_STRING  = 1
} opcua_nodeid_type_t;

typedef struct {
    uint16_t namespace_index;
    opcua_nodeid_type_t id_type;
    union {
        uint32_t numeric;
        char string[OPCUA_MAX_STRING_LEN];
    } id;
} opcua_nodeid_t;

/* QualifiedName */
typedef struct {
    uint16_t namespace_index;
    char name[OPCUA_MAX_STRING_LEN];
} opcua_qualified_name_t;

/* LocalizedText */
typedef struct {
    char text[OPCUA_MAX_STRING_LEN];
} opcua_localized_text_t;

/* Variant (任意のデータ値を格納) */
typedef struct {
    opcua_type_id_t type;
    union {
        bool     boolean;
        int8_t   sbyte_val;
        uint8_t  byte_val;
        int16_t  int16_val;
        uint16_t uint16_val;
        int32_t  int32_val;
        uint32_t uint32_val;
        int64_t  int64_val;
        uint64_t uint64_val;
        float    float_val;
        double   double_val;
        char     string_val[OPCUA_MAX_STRING_LEN];
        uint32_t status_val;
    } value;
} opcua_variant_t;

/* DataValue */
typedef struct {
    opcua_variant_t value;
    uint32_t status;
    uint64_t server_timestamp;
} opcua_datavalue_t;

/* TCP トランスポート メッセージヘッダ */
typedef struct {
    char msg_type[4];   /**< "HEL", "ACK", "ERR", "OPN", "CLO", "MSG" (ヌル終端付き) */
    char is_final;      /**< 'F' (Final), 'C' (Continuous), 'A' (Abort) */
    uint32_t msg_size;  /**< ヘッダを含むメッセージ総バイト数 */
} opcua_tcp_header_t;

/* Hello メッセージ */
typedef struct {
    uint32_t protocol_version;
    uint32_t receive_buffer_size;
    uint32_t send_buffer_size;
    uint32_t max_message_size;
    uint32_t max_chunk_count;
    char endpoint_url[OPCUA_MAX_STRING_LEN];
} opcua_hello_message_t;

/* Acknowledge メッセージ */
typedef struct {
    uint32_t protocol_version;
    uint32_t receive_buffer_size;
    uint32_t send_buffer_size;
    uint32_t max_message_size;
    uint32_t max_chunk_count;
} opcua_ack_message_t;

#ifdef __cplusplus
}
#endif

#endif /* OPCUA_TYPES_H */
