#include "embed_opcua/opcua_binary.h"
#include <string.h>

/* ---------------------------------------------------------
 * プリミティブ型エンコード (リトルエンディアン)
 * --------------------------------------------------------- */
size_t opcua_encode_u8(uint8_t val, uint8_t *dst, size_t cap) {
    if (!dst || cap < 1)
        return 0;
    dst[0] = val;
    return 1;
}

size_t opcua_encode_u16(uint16_t val, uint8_t *dst, size_t cap) {
    if (!dst || cap < 2)
        return 0;
    dst[0] = (uint8_t)(val & 0xFF);
    dst[1] = (uint8_t)((val >> 8) & 0xFF);
    return 2;
}

size_t opcua_encode_u32(uint32_t val, uint8_t *dst, size_t cap) {
    if (!dst || cap < 4)
        return 0;
    dst[0] = (uint8_t)(val & 0xFF);
    dst[1] = (uint8_t)((val >> 8) & 0xFF);
    dst[2] = (uint8_t)((val >> 16) & 0xFF);
    dst[3] = (uint8_t)((val >> 24) & 0xFF);
    return 4;
}

size_t opcua_encode_u64(uint64_t val, uint8_t *dst, size_t cap) {
    if (!dst || cap < 8)
        return 0;
    for (int i = 0; i < 8; ++i) {
        dst[i] = (uint8_t)((val >> (i * 8)) & 0xFF);
    }
    return 8;
}

size_t opcua_encode_float(float val, uint8_t *dst, size_t cap) {
    if (!dst || cap < 4)
        return 0;
    uint32_t u;
    memcpy(&u, &val, sizeof(float));
    return opcua_encode_u32(u, dst, cap);
}

size_t opcua_encode_double(double val, uint8_t *dst, size_t cap) {
    if (!dst || cap < 8)
        return 0;
    uint64_t u;
    memcpy(&u, &val, sizeof(double));
    return opcua_encode_u64(u, dst, cap);
}

size_t opcua_encode_string(const char *str, uint8_t *dst, size_t cap) {
    if (!dst || cap < 4)
        return 0;
    if (!str) {
        return opcua_encode_u32((uint32_t)-1, dst, cap); // Null String (-1)
    }

    size_t len = strlen(str);
    if (len > INT32_MAX || len > cap - 4)
        return 0;
    size_t off = opcua_encode_u32((uint32_t)len, dst, cap);
    memcpy(&dst[off], str, len);
    return off + len;
}

size_t opcua_encode_nodeid(const opcua_nodeid_t *nodeid, uint8_t *dst, size_t cap) {
    if (!dst || !nodeid || cap < 1)
        return 0;
    size_t off = 0;

    if (nodeid->id_type == OPCUA_NODEID_NUMERIC) {
        size_t need = nodeid->namespace_index == 0 && nodeid->id.numeric <= 255       ? 2
                      : nodeid->namespace_index <= 255 && nodeid->id.numeric <= 65535 ? 4
                                                                                      : 7;
        if (cap < need)
            return 0;
        if (nodeid->namespace_index == 0 && nodeid->id.numeric <= 255) {
            dst[off++] = 0x00; // TwoByte NodeId
            dst[off++] = (uint8_t)nodeid->id.numeric;
        } else if (nodeid->namespace_index <= 255 && nodeid->id.numeric <= 65535) {
            dst[off++] = 0x01; // FourByte NodeId
            dst[off++] = (uint8_t)nodeid->namespace_index;
            {
                size_t n = opcua_encode_u16((uint16_t)nodeid->id.numeric, &dst[off], cap - off);
                if (!n)
                    return 0;
                off += n;
            }
        } else {
            dst[off++] = 0x02; // Numeric NodeId
            {
                size_t n = opcua_encode_u16(nodeid->namespace_index, &dst[off], cap - off);
                if (!n)
                    return 0;
                off += n;
            }
            {
                size_t n = opcua_encode_u32(nodeid->id.numeric, &dst[off], cap - off);
                if (!n)
                    return 0;
                off += n;
            }
        }
    } else {
        if (nodeid->id_type != OPCUA_NODEID_STRING || cap < 7 ||
            !memchr(nodeid->id.string, 0, sizeof(nodeid->id.string)))
            return 0;
        dst[off++] = 0x03; // String NodeId
        {
            size_t n = opcua_encode_u16(nodeid->namespace_index, &dst[off], cap - off);
            if (!n)
                return 0;
            off += n;
        }
        {
            size_t n = opcua_encode_string(nodeid->id.string, &dst[off], cap - off);
            if (!n)
                return 0;
            off += n;
        }
    }
    return off;
}

size_t opcua_encode_variant(const opcua_variant_t *variant, uint8_t *dst, size_t cap) {
    if (!dst || !variant || cap < 1)
        return 0;
    size_t off = 0;
    dst[off++] = (uint8_t)variant->type;

    switch (variant->type) {
    case OPCUA_TYPE_NULL:
        break;
    case OPCUA_TYPE_BOOLEAN: {
        size_t n = opcua_encode_u8(variant->value.boolean ? 1 : 0, &dst[off], cap - off);
        if (!n)
            return 0;
        off += n;
    } break;
    case OPCUA_TYPE_SBYTE:
    case OPCUA_TYPE_BYTE: {
        size_t n = opcua_encode_u8(variant->value.byte_val, &dst[off], cap - off);
        if (!n)
            return 0;
        off += n;
    } break;
    case OPCUA_TYPE_INT16:
    case OPCUA_TYPE_UINT16: {
        size_t n = opcua_encode_u16(variant->value.uint16_val, &dst[off], cap - off);
        if (!n)
            return 0;
        off += n;
    } break;
    case OPCUA_TYPE_INT32:
    case OPCUA_TYPE_UINT32:
    case OPCUA_TYPE_STATUSCODE: {
        size_t n = opcua_encode_u32(variant->value.uint32_val, &dst[off], cap - off);
        if (!n)
            return 0;
        off += n;
    } break;
    case OPCUA_TYPE_INT64:
    case OPCUA_TYPE_UINT64:
    case OPCUA_TYPE_DATETIME: {
        size_t n = opcua_encode_u64(variant->value.uint64_val, &dst[off], cap - off);
        if (!n)
            return 0;
        off += n;
    } break;
    case OPCUA_TYPE_FLOAT: {
        size_t n = opcua_encode_float(variant->value.float_val, &dst[off], cap - off);
        if (!n)
            return 0;
        off += n;
    } break;
    case OPCUA_TYPE_DOUBLE: {
        size_t n = opcua_encode_double(variant->value.double_val, &dst[off], cap - off);
        if (!n)
            return 0;
        off += n;
    } break;
    case OPCUA_TYPE_STRING:
        if (!memchr(variant->value.string_val, 0, sizeof(variant->value.string_val)))
            return 0;
        {
            size_t n = opcua_encode_string(variant->value.string_val, &dst[off], cap - off);
            if (!n)
                return 0;
            off += n;
        }
        break;
    default:
        return 0;
    }
    return off;
}

size_t opcua_encode_datavalue(const opcua_datavalue_t *dv, uint8_t *dst, size_t cap) {
    if (!dst || !dv || cap < 1)
        return 0;
    size_t off = 0;

    uint8_t encoding_mask = 0x01; // Value present
    if (dv->status != OPCUA_STATUS_GOOD)
        encoding_mask |= 0x02; // Status present
    if (dv->server_timestamp != 0)
        encoding_mask |= 0x08; // Server timestamp present

    dst[off++] = encoding_mask;
    {
        size_t n = opcua_encode_variant(&dv->value, &dst[off], cap - off);
        if (!n)
            return 0;
        off += n;
    }

    if (encoding_mask & 0x02) {
        {
            size_t n = opcua_encode_u32(dv->status, &dst[off], cap - off);
            if (!n)
                return 0;
            off += n;
        }
    }
    if (encoding_mask & 0x08) {
        {
            size_t n = opcua_encode_u64(dv->server_timestamp, &dst[off], cap - off);
            if (!n)
                return 0;
            off += n;
        }
    }
    return off;
}

size_t opcua_encode_tcp_header(const opcua_tcp_header_t *hdr, uint8_t *dst, size_t cap) {
    if (!dst || !hdr || cap < 8)
        return 0;
    memcpy(&dst[0], hdr->msg_type, 3);
    dst[3] = (uint8_t)hdr->is_final;
    opcua_encode_u32(hdr->msg_size, &dst[4], cap - 4);
    return 8;
}

size_t opcua_encode_hello(const opcua_hello_message_t *hello, uint8_t *dst, size_t cap) {
    if (!dst || !hello || cap < 32)
        return 0;
    size_t payload_len = 20; // 5 * 4 bytes
    if (!memchr(hello->endpoint_url, 0, sizeof(hello->endpoint_url)))
        return 0;
    size_t url_len = strlen(hello->endpoint_url);
    if (url_len > cap - 32)
        return 0;
    payload_len += 4 + url_len;

    opcua_tcp_header_t hdr;
    memcpy(hdr.msg_type, "HEL", 3);
    hdr.is_final = 'F';
    hdr.msg_size = 8 + (uint32_t)payload_len;

    size_t off = opcua_encode_tcp_header(&hdr, dst, cap);
    {
        size_t n = opcua_encode_u32(hello->protocol_version, &dst[off], cap - off);
        if (!n)
            return 0;
        off += n;
    }
    {
        size_t n = opcua_encode_u32(hello->receive_buffer_size, &dst[off], cap - off);
        if (!n)
            return 0;
        off += n;
    }
    {
        size_t n = opcua_encode_u32(hello->send_buffer_size, &dst[off], cap - off);
        if (!n)
            return 0;
        off += n;
    }
    {
        size_t n = opcua_encode_u32(hello->max_message_size, &dst[off], cap - off);
        if (!n)
            return 0;
        off += n;
    }
    {
        size_t n = opcua_encode_u32(hello->max_chunk_count, &dst[off], cap - off);
        if (!n)
            return 0;
        off += n;
    }
    {
        size_t n = opcua_encode_string(hello->endpoint_url, &dst[off], cap - off);
        if (!n)
            return 0;
        off += n;
    }
    return off;
}

size_t opcua_encode_ack(const opcua_ack_message_t *ack, uint8_t *dst, size_t cap) {
    if (!dst || !ack || cap < 28)
        return 0;
    opcua_tcp_header_t hdr;
    memcpy(hdr.msg_type, "ACK", 3);
    hdr.is_final = 'F';
    hdr.msg_size = 8 + 20;

    size_t off = opcua_encode_tcp_header(&hdr, dst, cap);
    {
        size_t n = opcua_encode_u32(ack->protocol_version, &dst[off], cap - off);
        if (!n)
            return 0;
        off += n;
    }
    {
        size_t n = opcua_encode_u32(ack->receive_buffer_size, &dst[off], cap - off);
        if (!n)
            return 0;
        off += n;
    }
    {
        size_t n = opcua_encode_u32(ack->send_buffer_size, &dst[off], cap - off);
        if (!n)
            return 0;
        off += n;
    }
    {
        size_t n = opcua_encode_u32(ack->max_message_size, &dst[off], cap - off);
        if (!n)
            return 0;
        off += n;
    }
    {
        size_t n = opcua_encode_u32(ack->max_chunk_count, &dst[off], cap - off);
        if (!n)
            return 0;
        off += n;
    }
    return off;
}

size_t opcua_encode_error(uint32_t error_code, const char *reason, uint8_t *dst, size_t cap) {
    if (!dst || cap < 16)
        return 0;
    size_t r_len = reason ? strlen(reason) : 0;
    if (r_len > INT32_MAX || r_len > cap - 16)
        return 0;
    size_t payload_len = 4 + 4 + r_len;

    opcua_tcp_header_t hdr;
    memcpy(hdr.msg_type, "ERR", 3);
    hdr.is_final = 'F';
    hdr.msg_size = 8 + (uint32_t)payload_len;

    size_t off = opcua_encode_tcp_header(&hdr, dst, cap);
    {
        size_t n = opcua_encode_u32(error_code, &dst[off], cap - off);
        if (!n)
            return 0;
        off += n;
    }
    {
        size_t n = opcua_encode_string(reason, &dst[off], cap - off);
        if (!n)
            return 0;
        off += n;
    }
    return off;
}

/* ---------------------------------------------------------
 * デコード
 * --------------------------------------------------------- */
size_t opcua_decode_u8(const uint8_t *src, size_t len, uint8_t *val) {
    if (!src || !val || len < 1)
        return 0;
    *val = src[0];
    return 1;
}

size_t opcua_decode_u16(const uint8_t *src, size_t len, uint16_t *val) {
    if (!src || !val || len < 2)
        return 0;
    *val = (uint16_t)(src[0] | (src[1] << 8));
    return 2;
}

size_t opcua_decode_u32(const uint8_t *src, size_t len, uint32_t *val) {
    if (!src || !val || len < 4)
        return 0;
    *val = (uint32_t)(src[0] | (src[1] << 8) | (src[2] << 16) | ((uint32_t)src[3] << 24));
    return 4;
}

size_t opcua_decode_u64(const uint8_t *src, size_t len, uint64_t *val) {
    if (!src || !val || len < 8)
        return 0;
    uint64_t u = 0;
    for (int i = 0; i < 8; ++i) {
        u |= ((uint64_t)src[i] << (i * 8));
    }
    *val = u;
    return 8;
}

size_t opcua_decode_float(const uint8_t *src, size_t len, float *val) {
    if (!src || len < 4)
        return 0;
    uint32_t u;
    size_t off = opcua_decode_u32(src, len, &u);
    if (!off || !val)
        return 0;
    memcpy(val, &u, sizeof(float));
    return off;
}

size_t opcua_decode_double(const uint8_t *src, size_t len, double *val) {
    if (!src || len < 8)
        return 0;
    uint64_t u;
    size_t off = opcua_decode_u64(src, len, &u);
    if (!off || !val)
        return 0;
    memcpy(val, &u, sizeof(double));
    return off;
}

size_t opcua_decode_string(const uint8_t *src, size_t src_len, char *dst, size_t max_dst_len) {
    if (!src || src_len < 4)
        return 0;
    if (!src || !dst || max_dst_len == 0)
        return 0;
    uint32_t len = 0;
    size_t off = opcua_decode_u32(src, src_len, &len);

    if (len == (uint32_t)-1 || len == 0) {
        dst[0] = '\0';
        return off;
    }

    if (len > INT32_MAX || len > src_len - off || len >= max_dst_len)
        return 0;
    size_t copy_len = len;
    memcpy(dst, &src[off], copy_len);
    dst[copy_len] = '\0';
    return off + len;
}

size_t opcua_decode_nodeid(const uint8_t *src, size_t len, opcua_nodeid_t *nodeid) {
    if (!src || !nodeid || len < 1)
        return 0;
    size_t off = 0;
    uint8_t fmt = src[off++];

    memset(nodeid, 0, sizeof(opcua_nodeid_t));

    switch (fmt) {
    case 0x00: // TwoByte
        if (len < 2)
            return 0;
        nodeid->namespace_index = 0;
        nodeid->id_type = OPCUA_NODEID_NUMERIC;
        nodeid->id.numeric = src[off++];
        break;
    case 0x01: // FourByte
        if (len < 4)
            return 0;
        nodeid->namespace_index = src[off++];
        nodeid->id_type = OPCUA_NODEID_NUMERIC;
        uint16_t n16;
        {
            size_t n = opcua_decode_u16(&src[off], len - off, &n16);
            if (!n)
                return 0;
            off += n;
        }
        nodeid->id.numeric = n16;
        break;
    case 0x02: // Numeric
    {
        size_t n = opcua_decode_u16(&src[off], len - off, &nodeid->namespace_index);
        if (!n)
            return 0;
        off += n;
    }
        nodeid->id_type = OPCUA_NODEID_NUMERIC;
        {
            size_t n = opcua_decode_u32(&src[off], len - off, &nodeid->id.numeric);
            if (!n)
                return 0;
            off += n;
        }
        break;
    case 0x03: // String
    {
        size_t n = opcua_decode_u16(&src[off], len - off, &nodeid->namespace_index);
        if (!n)
            return 0;
        off += n;
    }
        nodeid->id_type = OPCUA_NODEID_STRING;
        {
            size_t n = opcua_decode_string(&src[off], len - off, nodeid->id.string,
                                           sizeof(nodeid->id.string));
            if (!n)
                return 0;
            off += n;
        }
        break;
    default:
        return 0;
    }
    return off;
}

size_t opcua_decode_variant(const uint8_t *src, size_t len, opcua_variant_t *variant) {
    if (!src || !variant || len < 1)
        return 0;
    size_t off = 0;
    variant->type = (opcua_type_id_t)src[off++];

    switch (variant->type) {
    case OPCUA_TYPE_NULL:
        break;
    case OPCUA_TYPE_BOOLEAN:
        if (len < 2)
            return 0;
        variant->value.boolean = (src[off++] != 0);
        break;
    case OPCUA_TYPE_SBYTE:
    case OPCUA_TYPE_BYTE: {
        size_t n = opcua_decode_u8(&src[off], len - off, &variant->value.byte_val);
        if (!n)
            return 0;
        off += n;
    } break;
    case OPCUA_TYPE_INT16:
    case OPCUA_TYPE_UINT16: {
        size_t n = opcua_decode_u16(&src[off], len - off, &variant->value.uint16_val);
        if (!n)
            return 0;
        off += n;
    } break;
    case OPCUA_TYPE_INT32:
    case OPCUA_TYPE_UINT32:
    case OPCUA_TYPE_STATUSCODE: {
        size_t n = opcua_decode_u32(&src[off], len - off, &variant->value.uint32_val);
        if (!n)
            return 0;
        off += n;
    } break;
    case OPCUA_TYPE_INT64:
    case OPCUA_TYPE_UINT64:
    case OPCUA_TYPE_DATETIME: {
        size_t n = opcua_decode_u64(&src[off], len - off, &variant->value.uint64_val);
        if (!n)
            return 0;
        off += n;
    } break;
    case OPCUA_TYPE_FLOAT: {
        size_t n = opcua_decode_float(&src[off], len - off, &variant->value.float_val);
        if (!n)
            return 0;
        off += n;
    } break;
    case OPCUA_TYPE_DOUBLE: {
        size_t n = opcua_decode_double(&src[off], len - off, &variant->value.double_val);
        if (!n)
            return 0;
        off += n;
    } break;
    case OPCUA_TYPE_STRING: {
        size_t n = opcua_decode_string(&src[off], len - off, variant->value.string_val,
                                       sizeof(variant->value.string_val));
        if (!n)
            return 0;
        off += n;
    } break;
    default:
        return 0;
    }
    return off;
}

size_t opcua_decode_datavalue(const uint8_t *src, size_t len, opcua_datavalue_t *dv) {
    if (!src || !dv || len < 1)
        return 0;
    size_t off = 0;
    uint8_t mask = src[off++];
    if (mask & ~0x0B)
        return 0; /* unsupported fields must not be silently skipped */

    memset(dv, 0, sizeof(opcua_datavalue_t));
    dv->status = OPCUA_STATUS_GOOD;

    if (mask & 0x01) {
        {
            size_t n = opcua_decode_variant(&src[off], len - off, &dv->value);
            if (!n)
                return 0;
            off += n;
        }
    }
    if (mask & 0x02) {
        {
            size_t n = opcua_decode_u32(&src[off], len - off, &dv->status);
            if (!n)
                return 0;
            off += n;
        }
    }
    if (mask & 0x08) {
        {
            size_t n = opcua_decode_u64(&src[off], len - off, &dv->server_timestamp);
            if (!n)
                return 0;
            off += n;
        }
    }
    return off;
}

size_t opcua_decode_tcp_header(const uint8_t *src, size_t len, opcua_tcp_header_t *hdr) {
    if (!src || !hdr || len < 8)
        return 0;
    memcpy(hdr->msg_type, src, 3);
    hdr->msg_type[3] = '\0';
    hdr->is_final = (char)src[3];
    opcua_decode_u32(&src[4], len - 4, &hdr->msg_size);
    return 8;
}

size_t opcua_decode_hello(const uint8_t *src, size_t len, opcua_hello_message_t *hello) {
    if (!src || !hello || len < 32)
        return 0;
    opcua_tcp_header_t hdr;
    size_t off = opcua_decode_tcp_header(src, len, &hdr);
    if (!off || memcmp(hdr.msg_type, "HEL", 3) || hdr.is_final != 'F' || hdr.msg_size < 32 ||
        hdr.msg_size > len)
        return 0;
    len = hdr.msg_size;

    {
        size_t n = opcua_decode_u32(&src[off], len - off, &hello->protocol_version);
        if (!n)
            return 0;
        off += n;
    }
    {
        size_t n = opcua_decode_u32(&src[off], len - off, &hello->receive_buffer_size);
        if (!n)
            return 0;
        off += n;
    }
    {
        size_t n = opcua_decode_u32(&src[off], len - off, &hello->send_buffer_size);
        if (!n)
            return 0;
        off += n;
    }
    {
        size_t n = opcua_decode_u32(&src[off], len - off, &hello->max_message_size);
        if (!n)
            return 0;
        off += n;
    }
    {
        size_t n = opcua_decode_u32(&src[off], len - off, &hello->max_chunk_count);
        if (!n)
            return 0;
        off += n;
    }
    {
        size_t n = opcua_decode_string(&src[off], len - off, hello->endpoint_url,
                                       sizeof(hello->endpoint_url));
        if (!n)
            return 0;
        off += n;
    }
    return off == len ? off : 0;
}

size_t opcua_decode_ack(const uint8_t *src, size_t len, opcua_ack_message_t *ack) {
    if (!src || !ack || len < 28)
        return 0;
    opcua_tcp_header_t hdr;
    size_t off = opcua_decode_tcp_header(src, len, &hdr);
    if (!off || memcmp(hdr.msg_type, "ACK", 3) || hdr.is_final != 'F' || hdr.msg_size < 28 ||
        hdr.msg_size > len)
        return 0;
    len = hdr.msg_size;

    {
        size_t n = opcua_decode_u32(&src[off], len - off, &ack->protocol_version);
        if (!n)
            return 0;
        off += n;
    }
    {
        size_t n = opcua_decode_u32(&src[off], len - off, &ack->receive_buffer_size);
        if (!n)
            return 0;
        off += n;
    }
    {
        size_t n = opcua_decode_u32(&src[off], len - off, &ack->send_buffer_size);
        if (!n)
            return 0;
        off += n;
    }
    {
        size_t n = opcua_decode_u32(&src[off], len - off, &ack->max_message_size);
        if (!n)
            return 0;
        off += n;
    }
    {
        size_t n = opcua_decode_u32(&src[off], len - off, &ack->max_chunk_count);
        if (!n)
            return 0;
        off += n;
    }
    return off == len ? off : 0;
}
