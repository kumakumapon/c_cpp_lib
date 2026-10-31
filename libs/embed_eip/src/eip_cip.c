#include "embed_eip/eip_cip.h"
#include <string.h>

/* Logical Segment Types (Class=0, Instance=1, Attribute=4) */
#define CIP_SEG_LOGICAL_CLASS_8      0x20
#define CIP_SEG_LOGICAL_CLASS_16     0x21
#define CIP_SEG_LOGICAL_INST_8       0x24
#define CIP_SEG_LOGICAL_INST_16      0x25
#define CIP_SEG_LOGICAL_ATTR_8       0x30
#define CIP_SEG_LOGICAL_ATTR_16      0x31

size_t cip_encode_epath(const cip_epath_t *path, uint8_t *dst) {
    if (!path || !dst) return 0;
    size_t off = 0;

    /* Class */
    if (path->class_id <= 0xFF) {
        dst[off++] = CIP_SEG_LOGICAL_CLASS_8;
        dst[off++] = (uint8_t)path->class_id;
    } else {
        dst[off++] = CIP_SEG_LOGICAL_CLASS_16;
        dst[off++] = 0x00; // Pad
        dst[off++] = (uint8_t)(path->class_id & 0xFF);
        dst[off++] = (uint8_t)((path->class_id >> 8) & 0xFF);
    }

    /* Instance */
    if (path->instance_id <= 0xFF) {
        dst[off++] = CIP_SEG_LOGICAL_INST_8;
        dst[off++] = (uint8_t)path->instance_id;
    } else {
        dst[off++] = CIP_SEG_LOGICAL_INST_16;
        dst[off++] = 0x00; // Pad
        dst[off++] = (uint8_t)(path->instance_id & 0xFF);
        dst[off++] = (uint8_t)((path->instance_id >> 8) & 0xFF);
    }

    /* Attribute (Optional) */
    if (path->has_attribute) {
        if (path->attribute_id <= 0xFF) {
            dst[off++] = CIP_SEG_LOGICAL_ATTR_8;
            dst[off++] = (uint8_t)path->attribute_id;
        } else {
            dst[off++] = CIP_SEG_LOGICAL_ATTR_16;
            dst[off++] = 0x00; // Pad
            dst[off++] = (uint8_t)(path->attribute_id & 0xFF);
            dst[off++] = (uint8_t)((path->attribute_id >> 8) & 0xFF);
        }
    }

    return off;
}

size_t cip_decode_epath(const uint8_t *src, size_t src_len, cip_epath_t *path) {
    if (!src || src_len < 4 || !path) return 0;

    memset(path, 0, sizeof(cip_epath_t));
    size_t off = 0;

    while (off < src_len) {
        uint8_t seg_type = src[off++];

        switch (seg_type) {
        case CIP_SEG_LOGICAL_CLASS_8:
            if (off >= src_len) return 0;
            path->class_id = src[off++];
            break;
        case CIP_SEG_LOGICAL_CLASS_16:
            if (off + 3 > src_len) return 0;
            off++; // skip pad
            path->class_id = (uint16_t)(src[off] | (src[off + 1] << 8));
            off += 2;
            break;

        case CIP_SEG_LOGICAL_INST_8:
            if (off >= src_len) return 0;
            path->instance_id = src[off++];
            break;
        case CIP_SEG_LOGICAL_INST_16:
            if (off + 3 > src_len) return 0;
            off++; // skip pad
            path->instance_id = (uint16_t)(src[off] | (src[off + 1] << 8));
            off += 2;
            break;

        case CIP_SEG_LOGICAL_ATTR_8:
            if (off >= src_len) return 0;
            path->attribute_id = src[off++];
            path->has_attribute = true;
            break;
        case CIP_SEG_LOGICAL_ATTR_16:
            if (off + 3 > src_len) return 0;
            off++; // skip pad
            path->attribute_id = (uint16_t)(src[off] | (src[off + 1] << 8));
            path->has_attribute = true;
            off += 2;
            break;

        default:
            return off; /* 未知セグメントまたはパディング到達 */
        }
    }

    return off;
}

size_t cip_encode_request(const cip_request_t *req, uint8_t *dst, size_t max_dst_len) {
    if (!req || !dst) return 0;
    if (req->data_len > 0 && !req->data) return 0; /* 長さだけがあって中身のない要求は作らない */

    uint8_t path_buf[32];
    size_t path_bytes = cip_encode_epath(&req->path, path_buf);
    uint8_t path_words = (uint8_t)((path_bytes + 1) / 2);

    size_t total_len = 2 + (path_words * 2) + req->data_len;
    if (total_len > max_dst_len) return 0;

    dst[0] = req->service;
    dst[1] = path_words;
    memcpy(&dst[2], path_buf, path_bytes);

    size_t off = 2 + (path_words * 2);
    if (req->data && req->data_len > 0) {
        memcpy(&dst[off], req->data, req->data_len);
    }

    return total_len;
}

bool cip_decode_request(const uint8_t *src, size_t src_len, cip_request_t *req) {
    if (!src || src_len < 2 || !req) return false;

    memset(req, 0, sizeof(cip_request_t));
    req->service = src[0];
    uint8_t path_words = src[1];
    size_t path_bytes = path_words * 2;

    if (2 + path_bytes > src_len) return false;

    cip_decode_epath(&src[2], path_bytes, &req->path);

    req->data = &src[2 + path_bytes];
    req->data_len = src_len - (2 + path_bytes);
    return true;
}

size_t cip_encode_response(const cip_response_t *resp, uint8_t *dst, size_t max_dst_len) {
    if (!resp || !dst) return 0;

    size_t total_len = 4 + resp->data_len;
    if (total_len > max_dst_len) return 0;

    dst[0] = (uint8_t)(resp->service | 0x80); // Response Service Code
    dst[1] = 0x00;                           // Reserved
    dst[2] = resp->general_status;
    dst[3] = 0x00;                           // Additional Status Size (Words)

    if (resp->data && resp->data_len > 0) {
        memcpy(&dst[4], resp->data, resp->data_len);
    }

    return total_len;
}

size_t cip_build_get_attribute_single(uint16_t class_id, uint16_t instance_id, uint16_t attr_id,
                                      uint8_t *dst, size_t max_dst_len) {
    cip_request_t req;
    memset(&req, 0, sizeof(req));
    req.service = CIP_SERVICE_GET_ATTRIBUTE_SINGLE;
    req.path.class_id = class_id;
    req.path.instance_id = instance_id;
    req.path.attribute_id = attr_id;
    req.path.has_attribute = true;
    return cip_encode_request(&req, dst, max_dst_len);
}

size_t cip_build_set_attribute_single(uint16_t class_id, uint16_t instance_id, uint16_t attr_id,
                                      const uint8_t *data, size_t data_len,
                                      uint8_t *dst, size_t max_dst_len) {
    cip_request_t req;
    memset(&req, 0, sizeof(req));
    req.service = CIP_SERVICE_SET_ATTRIBUTE_SINGLE;
    req.path.class_id = class_id;
    req.path.instance_id = instance_id;
    req.path.attribute_id = attr_id;
    req.path.has_attribute = true;
    req.data = data;
    req.data_len = data_len;
    return cip_encode_request(&req, dst, max_dst_len);
}
