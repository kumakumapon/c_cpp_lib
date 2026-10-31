#pragma once
#ifndef EIP_TYPES_H
#define EIP_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------
 * EtherNet/IP 定数
 * --------------------------------------------------------- */
#define EIP_DEFAULT_TCP_PORT        44818   /**< Explicit Messaging TCPポート */
#define EIP_DEFAULT_UDP_PORT        2222    /**< Implicit I/O UDPポート */
#define EIP_ENCAP_HEADER_SIZE       24      /**< カプセル化ヘッダ長 */
#define EIP_MAX_PACKET_SIZE         1024    /**< パケットバッファサイズ */
#define EIP_PROTOCOL_VERSION        1       /**< カプセル化プロトコルバージョン */

/* ---------------------------------------------------------
 * カプセル化コマンドコード (Encapsulation Commands)
 * --------------------------------------------------------- */
typedef enum {
    EIP_CMD_NOP                     = 0x0000,
    EIP_CMD_LIST_SERVICES           = 0x0004,
    EIP_CMD_LIST_IDENTITY           = 0x0063,
    EIP_CMD_LIST_INTERFACES         = 0x0064,
    EIP_CMD_REGISTER_SESSION        = 0x0065, /**< セッション登録 */
    EIP_CMD_UNREGISTER_SESSION      = 0x0066, /**< セッション切断 */
    EIP_CMD_SEND_RR_DATA            = 0x006F, /**< 要求・応答データ送信 (UCMM) */
    EIP_CMD_SEND_UNIT_DATA          = 0x0070, /**< 接続済みユニットデータ送信 */
    EIP_CMD_INDICATE_STATUS         = 0x0072,
    EIP_CMD_CANCEL                  = 0x0073
} eip_encap_cmd_t;

/* ---------------------------------------------------------
 * カプセル化エラーステータス (Encapsulation Status)
 * --------------------------------------------------------- */
typedef enum {
    EIP_STATUS_SUCCESS              = 0x00000000,
    EIP_STATUS_INVALID_CMD          = 0x00000001,
    EIP_STATUS_NO_MEMORY            = 0x00000002,
    EIP_STATUS_INCORRECT_DATA       = 0x00000003,
    EIP_STATUS_INVALID_SESSION      = 0x00000064,
    EIP_STATUS_INVALID_LENGTH       = 0x00000065,
    EIP_STATUS_UNSUPPORTED_VER      = 0x00000069
} eip_encap_status_t;

/* ---------------------------------------------------------
 * Common Packet Format (CPF) アイテムタイプ ID
 * --------------------------------------------------------- */
typedef enum {
    EIP_ITEM_NULL_ADDRESS           = 0x0000, /**< Null Address Item (UCMM) */
    EIP_ITEM_CONNECTED_ADDRESS      = 0x00A1, /**< Connected Address Item */
    EIP_ITEM_CONNECTED_DATA         = 0x00B1, /**< Connected Data Item */
    EIP_ITEM_UNCONNECTED_DATA       = 0x00B2, /**< Unconnected Message Data Item (CIP) */
    EIP_ITEM_SOCKADDR_O_TO_T        = 0x8000,
    EIP_ITEM_SOCKADDR_T_TO_O        = 0x8001
} eip_cpf_item_type_t;

/* ---------------------------------------------------------
 * CIP サービスコード (CIP Service Codes)
 * --------------------------------------------------------- */
typedef enum {
    CIP_SERVICE_GET_ATTRIBUTES_ALL      = 0x01,
    CIP_SERVICE_SET_ATTRIBUTES_ALL      = 0x02,
    CIP_SERVICE_GET_ATTRIBUTE_LIST      = 0x03,
    CIP_SERVICE_SET_ATTRIBUTE_LIST      = 0x04,
    CIP_SERVICE_RESET                   = 0x05,
    CIP_SERVICE_START                   = 0x06,
    CIP_SERVICE_STOP                    = 0x07,
    CIP_SERVICE_CREATE                  = 0x08,
    CIP_SERVICE_DELETE                  = 0x09,
    CIP_SERVICE_MULTIPLE_SERVICE_PACKET = 0x0A,
    CIP_SERVICE_APPLY_ATTRIBUTES        = 0x0D,
    CIP_SERVICE_GET_ATTRIBUTE_SINGLE    = 0x0E, /**< 単一アトリビュート取得 */
    CIP_SERVICE_SET_ATTRIBUTE_SINGLE    = 0x10, /**< 単一アトリビュート書き込み */
    CIP_SERVICE_FIND_NEXT_OBJECT_INST   = 0x11,
    CIP_SERVICE_FORWARD_OPEN            = 0x54, /**< 接続オープン (I/O) */
    CIP_SERVICE_FORWARD_CLOSE           = 0x4E, /**< 接続クローズ */
    CIP_SERVICE_READ_TAG                = 0x4C, /**< シンボル/タグ読み出し */
    CIP_SERVICE_WRITE_TAG               = 0x4D  /**< シンボル/タグ書き込み */
} cip_service_code_t;

/* ---------------------------------------------------------
 * CIP 一般ステータスコード (General Status Codes)
 * --------------------------------------------------------- */
typedef enum {
    CIP_STATUS_SUCCESS                  = 0x00,
    CIP_STATUS_CONNECTION_FAILURE       = 0x01,
    CIP_STATUS_RESOURCE_UNAVAILABLE     = 0x02,
    CIP_STATUS_INVALID_PARAMETER_VALUE  = 0x03,
    CIP_STATUS_PATH_SEGMENT_ERROR       = 0x04,
    CIP_STATUS_PATH_DESTINATION_UNKNOWN = 0x05,
    CIP_STATUS_PARTIAL_TRANSFER         = 0x06,
    CIP_STATUS_CONNECTION_LOST          = 0x07,
    CIP_STATUS_SERVICE_NOT_SUPPORTED    = 0x08,
    CIP_STATUS_INVALID_ATTRIBUTE_VALUE  = 0x09,
    CIP_STATUS_ATTRIBUTE_LIST_ERROR     = 0x0A,
    CIP_STATUS_ALREADY_IN_REQUESTED_MODE= 0x0B,
    CIP_STATUS_OBJECT_STATE_CONFLICT    = 0x0C,
    CIP_STATUS_OBJECT_ALREADY_EXISTS    = 0x0D,
    CIP_STATUS_ATTRIBUTE_NOT_SETTABLE   = 0x0E,
    CIP_STATUS_PRIVILEGE_VIOLATION      = 0x0F,
    CIP_STATUS_DEVICE_STATE_CONFLICT    = 0x10,
    CIP_STATUS_REPLY_DATA_TOO_LARGE     = 0x11,
    CIP_STATUS_FRAGMENTATION_PRIMITIVE  = 0x12,
    CIP_STATUS_CONFIG_TOO_SMALL         = 0x13,
    CIP_STATUS_NOT_ENOUGH_DATA          = 0x13, /**< 受信データが足りない (CIP Vol.1 の 0x13) */
    CIP_STATUS_TOO_MUCH_DATA            = 0x15, /**< 受信データが多すぎる (CIP Vol.1 の 0x15) */
    CIP_STATUS_UNDEFINED_ATTRIBUTE      = 0x14,
    CIP_STATUS_INVALID_MEMBER_ID        = 0x15,
    CIP_STATUS_MEMBER_NOT_SETTABLE      = 0x16,
    CIP_STATUS_KEY_FAILURE              = 0x1D,
    CIP_STATUS_PATH_SIZE_INVALID        = 0x26
} cip_general_status_t;

/* ---------------------------------------------------------
 * CIP 標準クラスID (Well-known Class IDs)
 * --------------------------------------------------------- */
typedef enum {
    CIP_CLASS_IDENTITY                  = 0x0001, /**< Identity Object */
    CIP_CLASS_MESSAGE_ROUTER            = 0x0002, /**< Message Router Object */
    CIP_CLASS_ASSEMBLY                  = 0x0004, /**< Assembly Object */
    CIP_CLASS_CONNECTION                = 0x0005, /**< Connection Object */
    CIP_CLASS_CONNECTION_MANAGER        = 0x0006, /**< Connection Manager */
    CIP_CLASS_REGISTER                  = 0x0007,
    CIP_CLASS_DISCRETE_INPUT_POINT      = 0x0008,
    CIP_CLASS_DISCRETE_OUTPUT_POINT     = 0x0009,
    CIP_CLASS_ANALOG_INPUT_POINT        = 0x000A,
    CIP_CLASS_ANALOG_OUTPUT_POINT       = 0x000B,
    CIP_CLASS_PARAMETER                 = 0x000F,
    CIP_CLASS_PARAMETER_GROUP           = 0x0010,
    CIP_CLASS_TCP_IP_INTERFACE          = 0x00F5, /**< TCP/IP Interface Object */
    CIP_CLASS_ETHERNET_LINK             = 0x00F6  /**< Ethernet Link Object */
} cip_class_id_t;

/* ---------------------------------------------------------
 * カプセル化ヘッダ構造体 (24バイト)
 * --------------------------------------------------------- */
typedef struct {
    uint16_t command;           /**< コマンドコード */
    uint16_t length;            /**< ヘッダ以降のデータ長 */
    uint32_t session_handle;    /**< セッションハンドル */
    uint32_t status;            /**< ステータスコード */
    uint8_t  sender_context[8]; /**< 送信元コンテキスト */
    uint32_t options;           /**< オプションフラグ */
} eip_encap_header_t;

/* ---------------------------------------------------------
 * Identity オブジェクト属性構造体
 * --------------------------------------------------------- */
typedef struct {
    uint16_t vendor_id;         /**< ベンダーID (ODVA登録ID) */
    uint16_t device_type;       /**< デバイスタイプ */
    uint16_t product_code;      /**< 製品コード */
    uint8_t  major_revision;    /**< メジャーリビジョン */
    uint8_t  minor_revision;    /**< マイナーリビジョン */
    uint16_t status;            /**< デバイスステータス */
    uint32_t serial_number;     /**< シリアル番号 */
    char     product_name[32];  /**< 製品名文字列 */
} cip_identity_attr_t;

#ifdef __cplusplus
}
#endif

#endif /* EIP_TYPES_H */
