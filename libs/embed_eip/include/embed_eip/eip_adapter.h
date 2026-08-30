#pragma once
#ifndef EIP_ADAPTER_H
#define EIP_ADAPTER_H

#include "eip_types.h"
#include "eip_encap.h"
#include "eip_cip.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EIP_MAX_SESSIONS            4
#define EIP_MAX_ASSEMBLIES          8
#define EIP_MAX_ASSEMBLY_SIZE       128

typedef struct {
    uint16_t instance_id;
    uint8_t data[EIP_MAX_ASSEMBLY_SIZE];
    size_t size;
    bool is_used;
} eip_assembly_t;

typedef struct {
    uint32_t session_handle;
    bool is_active;
} eip_session_t;

typedef struct {
    cip_identity_attr_t identity;
    eip_session_t sessions[EIP_MAX_SESSIONS];
    eip_assembly_t assemblies[EIP_MAX_ASSEMBLIES];
    uint32_t next_session_id;
} eip_adapter_t;

/**
 * @brief EtherNet/IP アダプタを初期化します。
 */
void eip_adapter_init(eip_adapter_t *adapter, const cip_identity_attr_t *identity);

/**
 * @brief I/O アセンブリオブジェクト (Assembly Instance) を追加登録します。
 */
bool eip_adapter_add_assembly(eip_adapter_t *adapter, uint16_t instance_id, size_t size);

/**
 * @brief アセンブリデータを書き込みます (センサ入力データの設定など)。
 */
bool eip_adapter_set_assembly_data(eip_adapter_t *adapter, uint16_t instance_id, const void *data, size_t size);

/**
 * @brief アセンブリデータを読み出します (PLC出力指示の読み取りなど)。
 */
bool eip_adapter_get_assembly_data(const eip_adapter_t *adapter, uint16_t instance_id, void *dest, size_t *size);

/**
 * @brief 受信したTCP EtherNet/IP パケットを処理し、応答パケットを生成します。
 * 
 * @param adapter アダプタインスタンス
 * @param req_data 受信バイト列
 * @param req_len 受信バイト長
 * @param resp_data 応答パケット格納先
 * @param max_resp_len 応答バッファサイズ
 * @return size_t 応答パケット長 (0の場合は応答不要)
 */
size_t eip_adapter_process_packet(eip_adapter_t *adapter,
                                  const uint8_t *req_data, size_t req_len,
                                  uint8_t *resp_data, size_t max_resp_len);

#ifdef __cplusplus
}
#endif

#endif /* EIP_ADAPTER_H */
