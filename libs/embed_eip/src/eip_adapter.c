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

static size_t process_cip_message(eip_adapter_t *adapter, const uint8_t *cip_req, size_t req_len,
                                  uint8_t *cip_resp, size_t max_resp_len) {
    cip_request_t req;
    if (!cip_decode_request(cip_req, req_len, &req)) {
        cip_response_t err_resp;
        memset(&err_resp, 0, sizeof(err_resp));
        err_resp.service = req.service;
        err_resp.general_status = CIP_STATUS_PATH_SEGMENT_ERROR;
        return cip_encode_response(&err_resp, cip_resp, max_resp_len);
    }

    cip_response_t resp;
    memset(&resp, 0, sizeof(resp));
    resp.service = req.service;

    uint8_t resp_payload[EIP_MAX_PACKET_SIZE];
    size_t payload_len = 0;

    switch (req.path.class_id) {
    /* ---------------------------------------------------------
     * Class 0x01: Identity Object
     * --------------------------------------------------------- */
    case CIP_CLASS_IDENTITY: {
        if (req.service == CIP_SERVICE_GET_ATTRIBUTE_SINGLE) {
            switch (req.path.attribute_id) {
            case 1: /* Vendor ID (uint16) */
                resp_payload[0] = (uint8_t)(adapter->identity.vendor_id & 0xFF);
                resp_payload[1] = (uint8_t)((adapter->identity.vendor_id >> 8) & 0xFF);
                payload_len = 2;
                break;
            case 2: /* Device Type (uint16) */
                resp_payload[0] = (uint8_t)(adapter->identity.device_type & 0xFF);
                resp_payload[1] = (uint8_t)((adapter->identity.device_type >> 8) & 0xFF);
                payload_len = 2;
                break;
            case 3: /* Product Code (uint16) */
                resp_payload[0] = (uint8_t)(adapter->identity.product_code & 0xFF);
                resp_payload[1] = (uint8_t)((adapter->identity.product_code >> 8) & 0xFF);
                payload_len = 2;
                break;
            case 4: /* Revision (Major, Minor) */
                resp_payload[0] = adapter->identity.major_revision;
                resp_payload[1] = adapter->identity.minor_revision;
                payload_len = 2;
                break;
            case 5: /* Status (uint16) */
                resp_payload[0] = (uint8_t)(adapter->identity.status & 0xFF);
                resp_payload[1] = (uint8_t)((adapter->identity.status >> 8) & 0xFF);
                payload_len = 2;
                break;
            case 6: /* Serial Number (uint32) */
                memcpy(resp_payload, &adapter->identity.serial_number, 4);
                payload_len = 4;
                break;
            case 7: /* Product Name (Short String: Len + Str) */
                resp_payload[0] = (uint8_t)strlen(adapter->identity.product_name);
                memcpy(&resp_payload[1], adapter->identity.product_name, resp_payload[0]);
                payload_len = 1 + resp_payload[0];
                break;
            default:
                resp.general_status = CIP_STATUS_UNDEFINED_ATTRIBUTE;
                return cip_encode_response(&resp, cip_resp, max_resp_len);
            }
            resp.general_status = CIP_STATUS_SUCCESS;
            resp.data = resp_payload;
            resp.data_len = payload_len;
            return cip_encode_response(&resp, cip_resp, max_resp_len);
        }
        break;
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

        if (!target_asm) {
            resp.general_status = CIP_STATUS_PATH_DESTINATION_UNKNOWN;
            return cip_encode_response(&resp, cip_resp, max_resp_len);
        }

        if (req.service == CIP_SERVICE_GET_ATTRIBUTE_SINGLE &&
            req.path.attribute_id == 3 /* Data */) {
            resp.general_status = CIP_STATUS_SUCCESS;
            resp.data = target_asm->data;
            resp.data_len = target_asm->size;
            return cip_encode_response(&resp, cip_resp, max_resp_len);
        } else if (req.service == CIP_SERVICE_SET_ATTRIBUTE_SINGLE &&
                   req.path.attribute_id == 3 /* Data */) {
            size_t copy_sz = req.data_len < target_asm->size ? req.data_len : target_asm->size;
            memcpy(target_asm->data, req.data, copy_sz);
            resp.general_status = CIP_STATUS_SUCCESS;
            return cip_encode_response(&resp, cip_resp, max_resp_len);
        }
        break;
    }

    default:
        resp.general_status = CIP_STATUS_SERVICE_NOT_SUPPORTED;
        return cip_encode_response(&resp, cip_resp, max_resp_len);
    }

    resp.general_status = CIP_STATUS_SERVICE_NOT_SUPPORTED;
    return cip_encode_response(&resp, cip_resp, max_resp_len);
}

size_t eip_adapter_process_packet(eip_adapter_t *adapter, const uint8_t *req_data, size_t req_len,
                                  uint8_t *resp_data, size_t max_resp_len) {
    if (!adapter || !req_data || req_len < EIP_ENCAP_HEADER_SIZE || !resp_data)
        return 0;

    eip_encap_header_t hdr;
    eip_decode_encap_header(req_data, req_len, &hdr);

    switch (hdr.command) {
    /* ---------------------------------------------------------
     * RegisterSession
     * --------------------------------------------------------- */
    case EIP_CMD_REGISTER_SESSION: {
        uint32_t session = register_session(adapter);
        if (session == 0) {
            hdr.status = EIP_STATUS_NO_MEMORY;
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
