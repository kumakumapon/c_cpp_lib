#include "embed_eip/eip_adapter.h"
#include <stdio.h>
#include <string.h>

void eip_adapter_init(eip_adapter_t *adapter, const cip_identity_attr_t *identity) {
    if (!adapter)
        return;
    memset(adapter, 0, sizeof(eip_adapter_t));
    if (identity) {
        adapter->identity = *identity;
    } else {
        adapter->identity.vendor_id = 0x0001;   // Default
        adapter->identity.device_type = 0x000C; // Communications Adapter
        adapter->identity.product_code = 0x0001;
        adapter->identity.major_revision = 1;
        adapter->identity.minor_revision = 0;
        adapter->identity.status = 0x0060;
        adapter->identity.serial_number = 0x12345678;
        snprintf(adapter->identity.product_name, sizeof(adapter->identity.product_name), "%s",
                 "Embedded EIP Adapter");
    }
    adapter->next_session_id = 0x1000;
}

bool eip_adapter_add_assembly(eip_adapter_t *adapter, uint16_t instance_id, size_t size) {
    if (!adapter || size > EIP_MAX_ASSEMBLY_SIZE)
        return false;

    /* 同じインスタンス ID を二重に登録すると、片方に到達できなくなる */
    for (size_t i = 0; i < EIP_MAX_ASSEMBLIES; ++i) {
        if (adapter->assemblies[i].is_used && adapter->assemblies[i].instance_id == instance_id)
            return false;
    }

    for (size_t i = 0; i < EIP_MAX_ASSEMBLIES; ++i) {
        if (!adapter->assemblies[i].is_used) {
            adapter->assemblies[i].instance_id = instance_id;
            adapter->assemblies[i].size = size;
            memset(adapter->assemblies[i].data, 0, size);
            adapter->assemblies[i].is_used = true;
            return true;
        }
    }
    return false;
}

bool eip_adapter_set_assembly_data(eip_adapter_t *adapter, uint16_t instance_id, const void *data,
                                   size_t size) {
    if (!adapter || !data)
        return false;

    for (size_t i = 0; i < EIP_MAX_ASSEMBLIES; ++i) {
        if (adapter->assemblies[i].is_used && adapter->assemblies[i].instance_id == instance_id) {
            size_t copy_sz =
                size < adapter->assemblies[i].size ? size : adapter->assemblies[i].size;
            memcpy(adapter->assemblies[i].data, data, copy_sz);
            return true;
        }
    }
    return false;
}

bool eip_adapter_get_assembly_data(const eip_adapter_t *adapter, uint16_t instance_id, void *dest,
                                   size_t *size) {
    if (!adapter || !dest || !size)
        return false;

    for (size_t i = 0; i < EIP_MAX_ASSEMBLIES; ++i) {
        if (adapter->assemblies[i].is_used && adapter->assemblies[i].instance_id == instance_id) {
            memcpy(dest, adapter->assemblies[i].data, adapter->assemblies[i].size);
            *size = adapter->assemblies[i].size;
            return true;
        }
    }
    return false;
}

static uint32_t register_session(eip_adapter_t *adapter) {
    for (size_t i = 0; i < EIP_MAX_SESSIONS; ++i) {
        if (!adapter->sessions[i].is_active) {
            adapter->sessions[i].session_handle = adapter->next_session_id++;
            adapter->sessions[i].is_active = true;
            return adapter->sessions[i].session_handle;
        }
    }
    return 0;
}

static bool validate_session(const eip_adapter_t *adapter, uint32_t session_handle) {
    for (size_t i = 0; i < EIP_MAX_SESSIONS; ++i) {
        if (adapter->sessions[i].is_active &&
            adapter->sessions[i].session_handle == session_handle) {
            return true;
        }
    }
    return false;
}

static void unregister_session(eip_adapter_t *adapter, uint32_t session_handle) {
    for (size_t i = 0; i < EIP_MAX_SESSIONS; ++i) {
        if (adapter->sessions[i].is_active &&
            adapter->sessions[i].session_handle == session_handle) {
            adapter->sessions[i].is_active = false;
            return;
        }
    }
}

static size_t cip_error(cip_response_t *resp, uint8_t status, uint8_t *cip_resp, size_t max_resp_len) {
    resp->general_status = status;
    resp->data = NULL;
    resp->data_len = 0;
    return cip_encode_response(resp, cip_resp, max_resp_len);
}

/* Identity オブジェクト (Class 0x01) の属性値を resp_payload に書き、その長さを返す。属性がなければ -1 */
static int identity_attribute(const eip_adapter_t *adapter, uint16_t attr, uint8_t *out) {
    const cip_identity_attr_t *id = &adapter->identity;
    switch (attr) {
    case 1: /* Vendor ID (UINT) */
        out[0] = (uint8_t)(id->vendor_id & 0xFF);
        out[1] = (uint8_t)((id->vendor_id >> 8) & 0xFF);
        return 2;
    case 2: /* Device Type (UINT) */
        out[0] = (uint8_t)(id->device_type & 0xFF);
        out[1] = (uint8_t)((id->device_type >> 8) & 0xFF);
        return 2;
    case 3: /* Product Code (UINT) */
        out[0] = (uint8_t)(id->product_code & 0xFF);
        out[1] = (uint8_t)((id->product_code >> 8) & 0xFF);
        return 2;
    case 4: /* Revision (Major, Minor) */
        out[0] = id->major_revision;
        out[1] = id->minor_revision;
        return 2;
    case 5: /* Status (WORD) */
        out[0] = (uint8_t)(id->status & 0xFF);
        out[1] = (uint8_t)((id->status >> 8) & 0xFF);
        return 2;
    case 6: /* Serial Number (UDINT): ホストのバイト順に依らずリトルエンディアン */
        out[0] = (uint8_t)(id->serial_number & 0xFF);
        out[1] = (uint8_t)((id->serial_number >> 8) & 0xFF);
        out[2] = (uint8_t)((id->serial_number >> 16) & 0xFF);
        out[3] = (uint8_t)((id->serial_number >> 24) & 0xFF);
        return 4;
    case 7: { /* Product Name (SHORT_STRING: 長さ + 文字列)。終端のない名前でも配列の外は読まない */
        size_t len = 0;
        while (len < sizeof(id->product_name) && id->product_name[len] != '\0')
            ++len;
        out[0] = (uint8_t)len;
        memcpy(&out[1], id->product_name, len);
        return (int)(1 + len);
    }
    default:
        return -1;
    }
}

static size_t process_cip_message(eip_adapter_t *adapter, const uint8_t *cip_req, size_t req_len,
                                  uint8_t *cip_resp, size_t max_resp_len) {
    cip_request_t req;
    memset(&req, 0, sizeof(req)); /* デコード失敗時にも service を未初期化で使わない */
    if (req_len >= 1)
        req.service = cip_req[0]; /* 応答のサービスコードに使う */

    cip_response_t resp;
    memset(&resp, 0, sizeof(resp));

    if (!cip_decode_request(cip_req, req_len, &req)) {
        resp.service = req.service;
        /* パスの大きさが要求の長さと合わない [CIP Vol.1: 0x26 Path size invalid] */
        return cip_error(&resp, CIP_STATUS_PATH_SIZE_INVALID, cip_resp, max_resp_len);
    }

    resp.service = req.service;

    uint8_t resp_payload[EIP_MAX_PACKET_SIZE];

    switch (req.path.class_id) {
    /* ---------------------------------------------------------
     * Class 0x01: Identity Object (インスタンスは 1 つだけ)
     * --------------------------------------------------------- */
    case CIP_CLASS_IDENTITY: {
        if (req.path.instance_id != 1)
            return cip_error(&resp, CIP_STATUS_PATH_DESTINATION_UNKNOWN, cip_resp, max_resp_len);

        if (req.service == CIP_SERVICE_GET_ATTRIBUTE_SINGLE) {
            int len = identity_attribute(adapter, req.path.attribute_id, resp_payload);
            if (len < 0)
                return cip_error(&resp, CIP_STATUS_UNDEFINED_ATTRIBUTE, cip_resp, max_resp_len);
            resp.general_status = CIP_STATUS_SUCCESS;
            resp.data = resp_payload;
            resp.data_len = (size_t)len;
            return cip_encode_response(&resp, cip_resp, max_resp_len);
        }
        if (req.service == CIP_SERVICE_SET_ATTRIBUTE_SINGLE) {
            /* Identity の属性 1..7 はすべて読み取り専用 */
            if (identity_attribute(adapter, req.path.attribute_id, resp_payload) < 0)
                return cip_error(&resp, CIP_STATUS_UNDEFINED_ATTRIBUTE, cip_resp, max_resp_len);
            return cip_error(&resp, CIP_STATUS_ATTRIBUTE_NOT_SETTABLE, cip_resp, max_resp_len);
        }
        return cip_error(&resp, CIP_STATUS_SERVICE_NOT_SUPPORTED, cip_resp, max_resp_len);
    }

    /* ---------------------------------------------------------
     * Class 0x04: Assembly Object (I/O Data)
     * --------------------------------------------------------- */
    case CIP_CLASS_ASSEMBLY: {
        uint16_t inst_id = req.path.instance_id;
        eip_assembly_t *target_asm = NULL;

        for (size_t i = 0; i < EIP_MAX_ASSEMBLIES; ++i) {
            if (adapter->assemblies[i].is_used && adapter->assemblies[i].instance_id == inst_id) {
                target_asm = &adapter->assemblies[i];
                break;
            }
        }

        if (!target_asm)
            return cip_error(&resp, CIP_STATUS_PATH_DESTINATION_UNKNOWN, cip_resp, max_resp_len);

        const bool is_get = req.service == CIP_SERVICE_GET_ATTRIBUTE_SINGLE;
        const bool is_set = req.service == CIP_SERVICE_SET_ATTRIBUTE_SINGLE;
        if (!is_get && !is_set)
            return cip_error(&resp, CIP_STATUS_SERVICE_NOT_SUPPORTED, cip_resp, max_resp_len);
        if (req.path.attribute_id != 3 /* Data */)
            return cip_error(&resp, CIP_STATUS_UNDEFINED_ATTRIBUTE, cip_resp, max_resp_len);

        if (is_get) {
            resp.general_status = CIP_STATUS_SUCCESS;
            resp.data = target_asm->data;
            resp.data_len = target_asm->size;
            return cip_encode_response(&resp, cip_resp, max_resp_len);
        }

        /* Set: データ長はアセンブリのサイズと一致している必要がある (部分的な書き込みはしない) */
        if (req.data_len < target_asm->size)
            return cip_error(&resp, CIP_STATUS_NOT_ENOUGH_DATA, cip_resp, max_resp_len);
        if (req.data_len > target_asm->size)
            return cip_error(&resp, CIP_STATUS_TOO_MUCH_DATA, cip_resp, max_resp_len);
        memcpy(target_asm->data, req.data, target_asm->size);
        return cip_error(&resp, CIP_STATUS_SUCCESS, cip_resp, max_resp_len);
    }

    /* 存在しないクラス */
    default:
        return cip_error(&resp, CIP_STATUS_PATH_DESTINATION_UNKNOWN, cip_resp, max_resp_len);
    }
}

size_t eip_adapter_process_packet(eip_adapter_t *adapter, const uint8_t *req_data, size_t req_len,
                                  uint8_t *resp_data, size_t max_resp_len) {
    if (!adapter || !req_data || req_len < EIP_ENCAP_HEADER_SIZE || !resp_data)
        return 0;
    /* 最小の応答 (ヘッダだけのエラー応答) も入らないバッファには書かない */
    if (max_resp_len < EIP_ENCAP_HEADER_SIZE)
        return 0;

    eip_encap_header_t hdr;
    if (eip_decode_encap_header(req_data, req_len, &hdr) == 0)
        return 0;

    /* ヘッダの自己申告長が実受信長を超えるパケットは、内部パーサに渡さず拒否する */
    if (hdr.length > req_len - EIP_ENCAP_HEADER_SIZE) {
        hdr.status = EIP_STATUS_INCORRECT_DATA;
        hdr.length = 0;
        return eip_encode_encap_header(&hdr, resp_data);
    }

    switch (hdr.command) {
    /* ---------------------------------------------------------
     * RegisterSession: ペイロードは プロトコルバージョン(2) + オプション(2)
     * --------------------------------------------------------- */
    case EIP_CMD_REGISTER_SESSION: {
        if (hdr.length != 4) {
            hdr.status = EIP_STATUS_INCORRECT_DATA;
            hdr.length = 0;
            return eip_encode_encap_header(&hdr, resp_data);
        }
        const uint16_t version =
            (uint16_t)(req_data[EIP_ENCAP_HEADER_SIZE] | (req_data[EIP_ENCAP_HEADER_SIZE + 1] << 8));
        if (version != EIP_PROTOCOL_VERSION) {
            /* 未対応のバージョン: セッションは作らず、サポートするバージョンを返す */
            size_t n = eip_build_register_session_resp(0, hdr.sender_context, resp_data, max_resp_len);
            if (n == 0)
                return 0;
            resp_data[8] = (uint8_t)(EIP_STATUS_UNSUPPORTED_VER & 0xFF);
            resp_data[9] = (uint8_t)((EIP_STATUS_UNSUPPORTED_VER >> 8) & 0xFF);
            resp_data[10] = 0;
            resp_data[11] = 0;
            return n;
        }

        uint32_t session = register_session(adapter);
        if (session == 0) {
            hdr.status = EIP_STATUS_NO_MEMORY;
            hdr.length = 0;
            return eip_encode_encap_header(&hdr, resp_data);
        }
        return eip_build_register_session_resp(session, hdr.sender_context, resp_data,
                                               max_resp_len);
    }

    /* ---------------------------------------------------------
     * UnRegisterSession
     * --------------------------------------------------------- */
    case EIP_CMD_UNREGISTER_SESSION: {
        unregister_session(adapter, hdr.session_handle);
        return 0; // UnRegisterSession は応答パケット送信なし
    }

    /* ---------------------------------------------------------
     * SendRRData (Explicit Messaging UCMM)
     * --------------------------------------------------------- */
    case EIP_CMD_SEND_RR_DATA: {
        if (!validate_session(adapter, hdr.session_handle)) {
            hdr.status = EIP_STATUS_INVALID_SESSION;
            hdr.length = 0;
            return eip_encode_encap_header(&hdr, resp_data);
        }

        const uint8_t *cip_data = NULL;
        size_t cip_len = 0;
        if (!eip_parse_send_rr_data(&req_data[EIP_ENCAP_HEADER_SIZE], hdr.length, &cip_data,
                                    &cip_len)) {
            hdr.status = EIP_STATUS_INCORRECT_DATA;
            hdr.length = 0;
            return eip_encode_encap_header(&hdr, resp_data);
        }

        uint8_t cip_resp[EIP_MAX_PACKET_SIZE];
        size_t cip_resp_len =
            process_cip_message(adapter, cip_data, cip_len, cip_resp, sizeof(cip_resp));

        return eip_build_send_rr_data(hdr.session_handle, hdr.sender_context, cip_resp,
                                      cip_resp_len, resp_data, max_resp_len);
    }

    default:
        hdr.status = EIP_STATUS_INVALID_CMD;
        hdr.length = 0;
        return eip_encode_encap_header(&hdr, resp_data);
    }
}
