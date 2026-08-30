#pragma once
#ifndef EIP_ENCAP_H
#define EIP_ENCAP_H

#include "eip_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------
 * カプセル化ヘッダ エンコード / デコード
 * --------------------------------------------------------- */
size_t eip_encode_encap_header(const eip_encap_header_t *hdr, uint8_t *dst);
size_t eip_decode_encap_header(const uint8_t *src, size_t src_len, eip_encap_header_t *hdr);

/* ---------------------------------------------------------
 * Common Packet Format (CPF) エンコード / デコード
 * --------------------------------------------------------- */

/**
 * @brief SendRRData (UCMM) パケットをカプセル化して構築します。
 * 
 * @param session_handle セッションハンドル
 * @param sender_context 送信元コンテキスト (8バイト)
 * @param cip_data CIPメッセージデータ
 * @param cip_len CIPメッセージ長
 * @param dst 出力バッファ
 * @param max_dst_len 出力バッファ長
 * @return size_t エンコードされた総バイト数 (ヘッダ含む)
 */
size_t eip_build_send_rr_data(uint32_t session_handle,
                              const uint8_t sender_context[8],
                              const uint8_t *cip_data, size_t cip_len,
                              uint8_t *dst, size_t max_dst_len);

/**
 * @brief RegisterSession 応答パケットを構築します。
 */
size_t eip_build_register_session_resp(uint32_t session_handle,
                                       const uint8_t sender_context[8],
                                       uint8_t *dst, size_t max_dst_len);

/**
 * @brief SendRRData から CIP データを抽出します。
 */
bool eip_parse_send_rr_data(const uint8_t *payload, size_t payload_len,
                            const uint8_t **cip_data, size_t *cip_len);

#ifdef __cplusplus
}
#endif

#endif /* EIP_ENCAP_H */
