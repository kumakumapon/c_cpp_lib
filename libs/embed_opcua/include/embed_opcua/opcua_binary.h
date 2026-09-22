#pragma once
#ifndef OPCUA_BINARY_H
#define OPCUA_BINARY_H

#include "opcua_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------
 * エンコード関数群
 * --------------------------------------------------------- */
size_t opcua_encode_u8(uint8_t val, uint8_t *dst, size_t cap);
size_t opcua_encode_u16(uint16_t val, uint8_t *dst, size_t cap);
size_t opcua_encode_u32(uint32_t val, uint8_t *dst, size_t cap);
size_t opcua_encode_u64(uint64_t val, uint8_t *dst, size_t cap);
size_t opcua_encode_float(float val, uint8_t *dst, size_t cap);
size_t opcua_encode_double(double val, uint8_t *dst, size_t cap);
size_t opcua_encode_string(const char *str, uint8_t *dst, size_t cap);
size_t opcua_encode_nodeid(const opcua_nodeid_t *nodeid, uint8_t *dst, size_t cap);
size_t opcua_encode_variant(const opcua_variant_t *variant, uint8_t *dst, size_t cap);
size_t opcua_encode_datavalue(const opcua_datavalue_t *dv, uint8_t *dst, size_t cap);

/* TCP トランスポートエンコード */
size_t opcua_encode_tcp_header(const opcua_tcp_header_t *hdr, uint8_t *dst, size_t cap);
size_t opcua_encode_hello(const opcua_hello_message_t *hello, uint8_t *dst, size_t cap);
size_t opcua_encode_ack(const opcua_ack_message_t *ack, uint8_t *dst, size_t cap);
size_t opcua_encode_error(uint32_t error_code, const char *reason, uint8_t *dst, size_t cap);

/* ---------------------------------------------------------
 * デコード関数群
 * --------------------------------------------------------- */
size_t opcua_decode_u8(const uint8_t *src, size_t len, uint8_t *val);
size_t opcua_decode_u16(const uint8_t *src, size_t len, uint16_t *val);
size_t opcua_decode_u32(const uint8_t *src, size_t len, uint32_t *val);
size_t opcua_decode_u64(const uint8_t *src, size_t len, uint64_t *val);
size_t opcua_decode_float(const uint8_t *src, size_t len, float *val);
size_t opcua_decode_double(const uint8_t *src, size_t len, double *val);
size_t opcua_decode_string(const uint8_t *src, size_t len, char *dst, size_t max_dst_len);
size_t opcua_decode_nodeid(const uint8_t *src, size_t len, opcua_nodeid_t *nodeid);
size_t opcua_decode_variant(const uint8_t *src, size_t len, opcua_variant_t *variant);
size_t opcua_decode_datavalue(const uint8_t *src, size_t len, opcua_datavalue_t *dv);

/* TCP トランスポートデコード */
size_t opcua_decode_tcp_header(const uint8_t *src, size_t len, opcua_tcp_header_t *hdr);
size_t opcua_decode_hello(const uint8_t *src, size_t len, opcua_hello_message_t *hello);
size_t opcua_decode_ack(const uint8_t *src, size_t len, opcua_ack_message_t *ack);

#ifdef __cplusplus
}
#endif

#endif /* OPCUA_BINARY_H */
