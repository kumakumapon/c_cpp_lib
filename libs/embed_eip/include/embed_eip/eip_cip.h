#pragma once
#ifndef EIP_CIP_H
#define EIP_CIP_H

#include "eip_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------
 * EPATH (Logical Segment) 構造体
 * --------------------------------------------------------- */
typedef struct {
    uint16_t class_id;      /**< クラスID */
    uint16_t instance_id;   /**< インスタンスID */
    uint16_t attribute_id;  /**< アトリビュートID (0は指定なし) */
    bool has_attribute;
} cip_epath_t;

/* ---------------------------------------------------------
 * CIP 要求 / 応答構造体
 * --------------------------------------------------------- */
typedef struct {
    uint8_t service;        /**< CIP サービスコード */
    cip_epath_t path;       /**< ターゲットEPATH */
    const uint8_t *data;    /**< 追加データ */
    size_t data_len;        /**< 追加データ長 */
} cip_request_t;

typedef struct {
    uint8_t service;        /**< サービスコード (0x80 | 元のサービス) */
    uint8_t general_status; /**< CIP 一般ステータス */
    uint16_t extended_status;/**< 拡張ステータス (通常0) */
    const uint8_t *data;    /**< 応答データ */
    size_t data_len;        /**< 応答データ長 */
} cip_response_t;

/* ---------------------------------------------------------
 * CIP パケット構築 / デコード関数群
 * --------------------------------------------------------- */

/**
 * @brief EPATH (Logical Class, Instance, Attribute) をエンコードします。
 */
size_t cip_encode_epath(const cip_epath_t *path, uint8_t *dst);

/**
 * @brief EPATH をデコードします。
 */
size_t cip_decode_epath(const uint8_t *src, size_t src_len, cip_epath_t *path);

/**
 * @brief CIP 要求メッセージをエンコードします。
 */
size_t cip_encode_request(const cip_request_t *req, uint8_t *dst, size_t max_dst_len);

/**
 * @brief 受信したCIPメッセージをデコードします。
 */
bool cip_decode_request(const uint8_t *src, size_t src_len, cip_request_t *req);

/**
 * @brief CIP 応答メッセージをエンコードします。
 */
size_t cip_encode_response(const cip_response_t *resp, uint8_t *dst, size_t max_dst_len);

/**
 * @brief Get_Attribute_Single リクエストを簡単に構築します。
 */
size_t cip_build_get_attribute_single(uint16_t class_id, uint16_t instance_id, uint16_t attr_id,
                                      uint8_t *dst, size_t max_dst_len);

/**
 * @brief Set_Attribute_Single リクエストを簡単に構築します。
 */
size_t cip_build_set_attribute_single(uint16_t class_id, uint16_t instance_id, uint16_t attr_id,
                                      const uint8_t *data, size_t data_len,
                                      uint8_t *dst, size_t max_dst_len);

#ifdef __cplusplus
}
#endif

#endif /* EIP_CIP_H */
