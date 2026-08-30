#include "embed_opcua/opcua_binary.h"
#include <string.h>

/* ---------------------------------------------------------
 * プリミティブ型エンコード (リトルエンディアン)
 * --------------------------------------------------------- */
size_t opcua_encode_u8(uint8_t val, uint8_t *dst) {
    if (!dst) return 0;
    dst[0] = val;
    return 1;
}

size_t opcua_encode_u16(uint16_t val, uint8_t *dst) {
    if (!dst) return 0;
    dst[0] = (uint8_t)(val & 0xFF);
    dst[1] = (uint8_t)((val >> 8) & 0xFF);
    return 2;
}

size_t opcua_encode_u32(uint32_t val, uint8_t *dst) {
    if (!dst) return 0;
    dst[0] = (uint8_t)(val & 0xFF);
    dst[1] = (uint8_t)((val >> 8) & 0xFF);
    dst[2] = (uint8_t)((val >> 16) & 0xFF);
    dst[3] = (uint8_t)((val >> 24) & 0xFF);
    return 4;
}

size_t opcua_encode_u64(uint64_t val, uint8_t *dst) {
    if (!dst) return 0;
    for (int i = 0; i < 8; ++i) {
        dst[i] = (uint8_t)((val >> (i * 8)) & 0xFF);
    }
    return 8;
}

size_t opcua_encode_float(float val, uint8_t *dst) {
    uint32_t u;
    memcpy(&u, &val, sizeof(float));
    return opcua_encode_u32(u, dst);
}

size_t opcua_encode_double(double val, uint8_t *dst) {
    uint64_t u;
    memcpy(&u, &val, sizeof(double));
    return opcua_encode_u64(u, dst);
}

size_t opcua_encode_string(const char *str, uint8_t *dst) {
    if (!dst) return 0;
    if (!str) {
        return opcua_encode_u32((uint32_t)-1, dst); // Null String (-1)
    }

    int32_t len = (int32_t)strlen(str);
    size_t off = opcua_encode_u32((uint32_t)len, dst);
    memcpy(&dst[off], str, len);
    return off + len;
}

size_t opcua_encode_nodeid(const opcua_nodeid_t *nodeid, uint8_t *dst) {
    if (!nodeid || !dst) return 0;
    size_t off = 0;

    if (nodeid->id_type == OPCUA_NODEID_NUMERIC) {
        if (nodeid->namespace_index == 0 && nodeid->id.numeric <= 255) {
            dst[off++] = 0x00; // TwoByte NodeId
            dst[off++] = (uint8_t)nodeid->id.numeric;
        } else if (nodeid->namespace_index <= 255 && nodeid->id.numeric <= 65535) {
            dst[off++] = 0x01; // FourByte NodeId
            dst[off++] = (uint8_t)nodeid->namespace_index;
            off += opcua_encode_u16((uint16_t)nodeid->id.numeric, &dst[off]);
        } else {
            dst[off++] = 0x02; // Numeric NodeId
            off += opcua_encode_u16(nodeid->namespace_index, &dst[off]);
            off += opcua_encode_u32(nodeid->id.numeric, &dst[off]);
        }
    } else {
        dst[off++] = 0x03; // String NodeId
        off += opcua_encode_u16(nodeid->namespace_index, &dst[off]);
        off += opcua_encode_string(nodeid->id.string, &dst[off]);
    }
    return off;
}

size_t opcua_encode_variant(const opcua_variant_t *variant, uint8_t *dst) {
    if (!variant || !dst) return 0;
    size_t off = 0;
    dst[off++] = (uint8_t)variant->type;

    switch (variant->type) {
    case OPCUA_TYPE_BOOLEAN:
        off += opcua_encode_u8(variant->value.boolean ? 1 : 0, &dst[off]);
        break;
    case OPCUA_TYPE_SBYTE:
    case OPCUA_TYPE_BYTE:
        off += opcua_encode_u8(variant->value.byte_val, &dst[off]);
        break;
    case OPCUA_TYPE_INT16:
    case OPCUA_TYPE_UINT16:
        off += opcua_encode_u16(variant->value.uint16_val, &dst[off]);
        break;
    case OPCUA_TYPE_INT32:
    case OPCUA_TYPE_UINT32:
    case OPCUA_TYPE_STATUSCODE:
        off += opcua_encode_u32(variant->value.uint32_val, &dst[off]);
        break;
    case OPCUA_TYPE_INT64:
    case OPCUA_TYPE_UINT64:
    case OPCUA_TYPE_DATETIME:
        off += opcua_encode_u64(variant->value.uint64_val, &dst[off]);
        break;
    case OPCUA_TYPE_FLOAT:
        off += opcua_encode_float(variant->value.float_val, &dst[off]);
        break;
    case OPCUA_TYPE_DOUBLE:
        off += opcua_encode_double(variant->value.double_val, &dst[off]);
        break;
    case OPCUA_TYPE_STRING:
        off += opcua_encode_string(variant->value.string_val, &dst[off]);
        break;
    default:
        break;
    }
    return off;
}

size_t opcua_encode_datavalue(const opcua_datavalue_t *dv, uint8_t *dst) {
    if (!dv || !dst) return 0;
    size_t off = 0;

    uint8_t encoding_mask = 0x01; // Value present
    if (dv->status != OPCUA_STATUS_GOOD) encoding_mask |= 0x02; // Status present
    if (dv->server_timestamp != 0) encoding_mask |= 0x08; // Server timestamp present

    dst[off++] = encoding_mask;
    off += opcua_encode_variant(&dv->value, &dst[off]);

    if (encoding_mask & 0x02) {
        off += opcua_encode_u32(dv->status, &dst[off]);
    }
    if (encoding_mask & 0x08) {
        off += opcua_encode_u64(dv->server_timestamp, &dst[off]);
    }
    return off;
}

size_t opcua_encode_tcp_header(const opcua_tcp_header_t *hdr, uint8_t *dst) {
    if (!hdr || !dst) return 0;
    memcpy(&dst[0], hdr->msg_type, 3);
    dst[3] = (uint8_t)hdr->is_final;
    opcua_encode_u32(hdr->msg_size, &dst[4]);
    return 8;
}

size_t opcua_encode_hello(const opcua_hello_message_t *hello, uint8_t *dst) {
    if (!hello || !dst) return 0;
    size_t payload_len = 20; // 5 * 4 bytes
    int32_t url_len = (int32_t)strlen(hello->endpoint_url);
    payload_len += 4 + url_len;

    opcua_tcp_header_t hdr;
    memcpy(hdr.msg_type, "HEL", 3);
    hdr.is_final = 'F';
    hdr.msg_size = 8 + (uint32_t)payload_len;

    size_t off = opcua_encode_tcp_header(&hdr, dst);
    off += opcua_encode_u32(hello->protocol_version, &dst[off]);
    off += opcua_encode_u32(hello->receive_buffer_size, &dst[off]);
    off += opcua_encode_u32(hello->send_buffer_size, &dst[off]);
    off += opcua_encode_u32(hello->max_message_size, &dst[off]);
    off += opcua_encode_u32(hello->max_chunk_count, &dst[off]);
    off += opcua_encode_string(hello->endpoint_url, &dst[off]);
    return off;
}

size_t opcua_encode_ack(const opcua_ack_message_t *ack, uint8_t *dst) {
    if (!ack || !dst) return 0;
    opcua_tcp_header_t hdr;
    memcpy(hdr.msg_type, "ACK", 3);
    hdr.is_final = 'F';
    hdr.msg_size = 8 + 20;

    size_t off = opcua_encode_tcp_header(&hdr, dst);
    off += opcua_encode_u32(ack->protocol_version, &dst[off]);
    off += opcua_encode_u32(ack->receive_buffer_size, &dst[off]);
    off += opcua_encode_u32(ack->send_buffer_size, &dst[off]);
    off += opcua_encode_u32(ack->max_message_size, &dst[off]);
    off += opcua_encode_u32(ack->max_chunk_count, &dst[off]);
    return off;
}

size_t opcua_encode_error(uint32_t error_code, const char *reason, uint8_t *dst) {
    if (!dst) return 0;
    int32_t r_len = reason ? (int32_t)strlen(reason) : 0;
    size_t payload_len = 4 + 4 + r_len;

    opcua_tcp_header_t hdr;
    memcpy(hdr.msg_type, "ERR", 3);
    hdr.is_final = 'F';
    hdr.msg_size = 8 + (uint32_t)payload_len;

    size_t off = opcua_encode_tcp_header(&hdr, dst);
    off += opcua_encode_u32(error_code, &dst[off]);
    off += opcua_encode_string(reason, &dst[off]);
    return off;
}

/* ---------------------------------------------------------
 * デコード
 * --------------------------------------------------------- */
size_t opcua_decode_u8(const uint8_t *src, uint8_t *val) {
    if (!src || !val) return 0;
    *val = src[0];
    return 1;
}

size_t opcua_decode_u16(const uint8_t *src, uint16_t *val) {
    if (!src || !val) return 0;
    *val = (uint16_t)(src[0] | (src[1] << 8));
    return 2;
}

size_t opcua_decode_u32(const uint8_t *src, uint32_t *val) {
    if (!src || !val) return 0;
    *val = (uint32_t)(src[0] | (src[1] << 8) | (src[2] << 16) | (src[3] << 24));
    return 4;
}

size_t opcua_decode_u64(const uint8_t *src, uint64_t *val) {
    if (!src || !val) return 0;
    uint64_t u = 0;
    for (int i = 0; i < 8; ++i) {
        u |= ((uint64_t)src[i] << (i * 8));
    }
    *val = u;
    return 8;
}

size_t opcua_decode_float(const uint8_t *src, float *val) {
    uint32_t u;
    size_t off = opcua_decode_u32(src, &u);
    memcpy(val, &u, sizeof(float));
    return off;
}

size_t opcua_decode_double(const uint8_t *src, double *val) {
    uint64_t u;
    size_t off = opcua_decode_u64(src, &u);
    memcpy(val, &u, sizeof(double));
    return off;
}

size_t opcua_decode_string(const uint8_t *src, char *dst, size_t max_dst_len) {
    if (!src || !dst || max_dst_len == 0) return 0;
    uint32_t len = 0;
    size_t off = opcua_decode_u32(src, &len);

    if (len == (uint32_t)-1 || len == 0) {
        dst[0] = '\0';
        return off;
    }

    size_t copy_len = len < (max_dst_len - 1) ? len : (max_dst_len - 1);
    memcpy(dst, &src[off], copy_len);
    dst[copy_len] = '\0';
    return off + len;
}

size_t opcua_decode_nodeid(const uint8_t *src, opcua_nodeid_t *nodeid) {
    if (!src || !nodeid) return 0;
    size_t off = 0;
    uint8_t fmt = src[off++];

    memset(nodeid, 0, sizeof(opcua_nodeid_t));

    switch (fmt & 0x0F) {
    case 0x00: // TwoByte
        nodeid->namespace_index = 0;
        nodeid->id_type = OPCUA_NODEID_NUMERIC;
        nodeid->id.numeric = src[off++];
        break;
    case 0x01: // FourByte
        nodeid->namespace_index = src[off++];
        nodeid->id_type = OPCUA_NODEID_NUMERIC;
        uint16_t n16;
        off += opcua_decode_u16(&src[off], &n16);
        nodeid->id.numeric = n16;
        break;
    case 0x02: // Numeric
        off += opcua_decode_u16(&src[off], &nodeid->namespace_index);
        nodeid->id_type = OPCUA_NODEID_NUMERIC;
        off += opcua_decode_u32(&src[off], &nodeid->id.numeric);
        break;
    case 0x03: // String
        off += opcua_decode_u16(&src[off], &nodeid->namespace_index);
        nodeid->id_type = OPCUA_NODEID_STRING;
        off += opcua_decode_string(&src[off], nodeid->id.string, sizeof(nodeid->id.string));
        break;
    default:
        break;
    }
    return off;
}

size_t opcua_decode_variant(const uint8_t *src, opcua_variant_t *variant) {
    if (!src || !variant) return 0;
    size_t off = 0;
    variant->type = (opcua_type_id_t)src[off++];

    switch (variant->type) {
    case OPCUA_TYPE_BOOLEAN:
        variant->value.boolean = (src[off++] != 0);
        break;
    case OPCUA_TYPE_SBYTE:
    case OPCUA_TYPE_BYTE:
        off += opcua_decode_u8(&src[off], &variant->value.byte_val);
        break;
    case OPCUA_TYPE_INT16:
    case OPCUA_TYPE_UINT16:
        off += opcua_decode_u16(&src[off], &variant->value.uint16_val);
        break;
    case OPCUA_TYPE_INT32:
    case OPCUA_TYPE_UINT32:
    case OPCUA_TYPE_STATUSCODE:
        off += opcua_decode_u32(&src[off], &variant->value.uint32_val);
        break;
    case OPCUA_TYPE_INT64:
    case OPCUA_TYPE_UINT64:
    case OPCUA_TYPE_DATETIME:
        off += opcua_decode_u64(&src[off], &variant->value.uint64_val);
        break;
    case OPCUA_TYPE_FLOAT:
        off += opcua_decode_float(&src[off], &variant->value.float_val);
        break;
    case OPCUA_TYPE_DOUBLE:
        off += opcua_decode_double(&src[off], &variant->value.double_val);
        break;
    case OPCUA_TYPE_STRING:
        off += opcua_decode_string(&src[off], variant->value.string_val, sizeof(variant->value.string_val));
        break;
    default:
        break;
    }
    return off;
}

size_t opcua_decode_datavalue(const uint8_t *src, opcua_datavalue_t *dv) {
    if (!src || !dv) return 0;
    size_t off = 0;
    uint8_t mask = src[off++];

    memset(dv, 0, sizeof(opcua_datavalue_t));
    dv->status = OPCUA_STATUS_GOOD;

    if (mask & 0x01) {
        off += opcua_decode_variant(&src[off], &dv->value);
    }
    if (mask & 0x02) {
        off += opcua_decode_u32(&src[off], &dv->status);
    }
    if (mask & 0x08) {
        off += opcua_decode_u64(&src[off], &dv->server_timestamp);
    }
    return off;
}

size_t opcua_decode_tcp_header(const uint8_t *src, size_t len, opcua_tcp_header_t *hdr) {
    if (!src || len < 8 || !hdr) return 0;
    memcpy(hdr->msg_type, src, 3);
    hdr->msg_type[3] = '\0';
    hdr->is_final = (char)src[3];
    opcua_decode_u32(&src[4], &hdr->msg_size);
    return 8;
}

size_t opcua_decode_hello(const uint8_t *src, size_t len, opcua_hello_message_t *hello) {
    if (!src || len < 28 || !hello) return 0;
    opcua_tcp_header_t hdr;
    size_t off = opcua_decode_tcp_header(src, len, &hdr);

    off += opcua_decode_u32(&src[off], &hello->protocol_version);
    off += opcua_decode_u32(&src[off], &hello->receive_buffer_size);
    off += opcua_decode_u32(&src[off], &hello->send_buffer_size);
    off += opcua_decode_u32(&src[off], &hello->max_message_size);
    off += opcua_decode_u32(&src[off], &hello->max_chunk_count);
    off += opcua_decode_string(&src[off], hello->endpoint_url, sizeof(hello->endpoint_url));
    return off;
}

size_t opcua_decode_ack(const uint8_t *src, size_t len, opcua_ack_message_t *ack) {
    if (!src || len < 28 || !ack) return 0;
    opcua_tcp_header_t hdr;
    size_t off = opcua_decode_tcp_header(src, len, &hdr);

    off += opcua_decode_u32(&src[off], &ack->protocol_version);
    off += opcua_decode_u32(&src[off], &ack->receive_buffer_size);
    off += opcua_decode_u32(&src[off], &ack->send_buffer_size);
    off += opcua_decode_u32(&src[off], &ack->max_message_size);
    off += opcua_decode_u32(&src[off], &ack->max_chunk_count);
    return off;
}
