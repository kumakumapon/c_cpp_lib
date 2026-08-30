#include "embed_eip/eip_encap.h"
#include <string.h>

static inline void encode_u16(uint16_t val, uint8_t *dst) {
    dst[0] = (uint8_t)(val & 0xFF);
    dst[1] = (uint8_t)((val >> 8) & 0xFF);
}

static inline void encode_u32(uint32_t val, uint8_t *dst) {
    dst[0] = (uint8_t)(val & 0xFF);
    dst[1] = (uint8_t)((val >> 8) & 0xFF);
    dst[2] = (uint8_t)((val >> 16) & 0xFF);
    dst[3] = (uint8_t)((val >> 24) & 0xFF);
}

static inline uint16_t decode_u16(const uint8_t *src) {
    return (uint16_t)(src[0] | (src[1] << 8));
}

static inline uint32_t decode_u32(const uint8_t *src) {
    return (uint32_t)(src[0] | (src[1] << 8) | (src[2] << 16) | (src[3] << 24));
}

size_t eip_encode_encap_header(const eip_encap_header_t *hdr, uint8_t *dst) {
    if (!hdr || !dst) return 0;

    encode_u16(hdr->command, &dst[0]);
    encode_u16(hdr->length, &dst[2]);
    encode_u32(hdr->session_handle, &dst[4]);
    encode_u32(hdr->status, &dst[8]);
    memcpy(&dst[12], hdr->sender_context, 8);
    encode_u32(hdr->options, &dst[20]);

    return EIP_ENCAP_HEADER_SIZE;
}

size_t eip_decode_encap_header(const uint8_t *src, size_t src_len, eip_encap_header_t *hdr) {
    if (!src || src_len < EIP_ENCAP_HEADER_SIZE || !hdr) return 0;

    hdr->command = decode_u16(&src[0]);
    hdr->length = decode_u16(&src[2]);
    hdr->session_handle = decode_u32(&src[4]);
    hdr->status = decode_u32(&src[8]);
    memcpy(hdr->sender_context, &src[12], 8);
    hdr->options = decode_u32(&src[20]);

    return EIP_ENCAP_HEADER_SIZE;
}

size_t eip_build_register_session_resp(uint32_t session_handle,
                                       const uint8_t sender_context[8],
                                       uint8_t *dst, size_t max_dst_len) {
    if (!dst || max_dst_len < EIP_ENCAP_HEADER_SIZE + 4) return 0;

    eip_encap_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.command = EIP_CMD_REGISTER_SESSION;
    hdr.length = 4;
    hdr.session_handle = session_handle;
    hdr.status = EIP_STATUS_SUCCESS;
    if (sender_context) {
        memcpy(hdr.sender_context, sender_context, 8);
    }

    size_t off = eip_encode_encap_header(&hdr, dst);
    encode_u16(EIP_PROTOCOL_VERSION, &dst[off]);
    encode_u16(0x0000 /* Options */, &dst[off + 2]);
    return off + 4;
}

size_t eip_build_send_rr_data(uint32_t session_handle,
                              const uint8_t sender_context[8],
                              const uint8_t *cip_data, size_t cip_len,
                              uint8_t *dst, size_t max_dst_len) {
    if (!dst || !cip_data || cip_len == 0) return 0;

    /* CPF (Common Packet Format) ペイロード長:
     * Interface Handle (4B: 0x00000000)
     * Timeout (2B: 0x0000)
     * Item Count (2B: 2)
     *   Item 1: Null Address (Type 2B=0x0000, Len 2B=0)
     *   Item 2: Unconnected Data (Type 2B=0x00B2, Len 2B=cip_len, Data cip_len)
     */
    size_t cpf_len = 4 + 2 + 2 + 4 + 4 + cip_len;
    if (EIP_ENCAP_HEADER_SIZE + cpf_len > max_dst_len) return 0;

    eip_encap_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.command = EIP_CMD_SEND_RR_DATA;
    hdr.length = (uint16_t)cpf_len;
    hdr.session_handle = session_handle;
    hdr.status = EIP_STATUS_SUCCESS;
    if (sender_context) {
        memcpy(hdr.sender_context, sender_context, 8);
    }

    size_t off = eip_encode_encap_header(&hdr, dst);

    /* Interface Handle (0) & Timeout (0) */
    encode_u32(0, &dst[off]); off += 4;
    encode_u16(0, &dst[off]); off += 2;

    /* Item Count = 2 */
    encode_u16(2, &dst[off]); off += 2;

    /* Item 1: Null Address Item */
    encode_u16(EIP_ITEM_NULL_ADDRESS, &dst[off]); off += 2;
    encode_u16(0, &dst[off]); off += 2;

    /* Item 2: Unconnected Data Item */
    encode_u16(EIP_ITEM_UNCONNECTED_DATA, &dst[off]); off += 2;
    encode_u16((uint16_t)cip_len, &dst[off]); off += 2;
    memcpy(&dst[off], cip_data, cip_len); off += cip_len;

    return off;
}

bool eip_parse_send_rr_data(const uint8_t *payload, size_t payload_len,
                            const uint8_t **cip_data, size_t *cip_len) {
    if (!payload || payload_len < 16 || !cip_data || !cip_len) return false;

    /* Skip Interface Handle (4B) & Timeout (2B) */
    size_t off = 6;
    uint16_t item_count = decode_u16(&payload[off]); off += 2;

    for (uint16_t i = 0; i < item_count; ++i) {
        if (off + 4 > payload_len) return false;

        uint16_t item_type = decode_u16(&payload[off]); off += 2;
        uint16_t item_len  = decode_u16(&payload[off]); off += 2;

        if (off + item_len > payload_len) return false;

        if (item_type == EIP_ITEM_UNCONNECTED_DATA || item_type == EIP_ITEM_CONNECTED_DATA) {
            *cip_data = &payload[off];
            *cip_len = item_len;
            return true;
        }
        off += item_len;
    }

    return false;
}
