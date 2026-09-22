#include "embed_opcua/opcua_server.h"
#include <stdio.h>
#include <string.h>

void opcua_server_init(opcua_server_t *server, const char *endpoint_url) {
    if (!server)
        return;
    memset(server, 0, sizeof(opcua_server_t));
    if (endpoint_url) {
        strncpy(server->endpoint_url, endpoint_url, sizeof(server->endpoint_url) - 1);
    } else {
        snprintf(server->endpoint_url, sizeof(server->endpoint_url), "%s",
                 "opc.tcp://localhost:4840");
    }
}

static opcua_node_t *find_node(opcua_server_t *server, const opcua_nodeid_t *node_id) {
    if (!server || !node_id)
        return NULL;

    for (size_t i = 0; i < server->node_count; ++i) {
        opcua_node_t *n = &server->nodes[i];
        if (!n->is_used)
            continue;

        if (n->node_id.namespace_index == node_id->namespace_index &&
            n->node_id.id_type == node_id->id_type) {
            if (node_id->id_type == OPCUA_NODEID_NUMERIC) {
                if (n->node_id.id.numeric == node_id->id.numeric) {
                    return n;
                }
            } else {
                if (strcmp(n->node_id.id.string, node_id->id.string) == 0) {
                    return n;
                }
            }
        }
    }
    return NULL;
}

bool opcua_server_add_variable(opcua_server_t *server, const opcua_nodeid_t *node_id,
                               const char *browse_name, const opcua_variant_t *initial_value,
                               uint8_t access_level) {
    if (!server || !node_id || server->node_count >= OPCUA_MAX_NODES) {
        return false;
    }

    if (find_node(server, node_id)) {
        return false; /* 重複 */
    }

    opcua_node_t *node = &server->nodes[server->node_count++];
    memset(node, 0, sizeof(opcua_node_t));
    node->is_used = true;
    node->node_id = *node_id;
    node->node_class = OPCUA_NODECLASS_VARIABLE;
    node->access_level = access_level;

    if (browse_name) {
        node->browse_name.namespace_index = node_id->namespace_index;
        strncpy(node->browse_name.name, browse_name, sizeof(node->browse_name.name) - 1);
        strncpy(node->display_name.text, browse_name, sizeof(node->display_name.text) - 1);
    }

    if (initial_value) {
        node->value.value = *initial_value;
        node->value.status = OPCUA_STATUS_GOOD;
    }

    return true;
}

bool opcua_server_write_value(opcua_server_t *server, const opcua_nodeid_t *node_id,
                              const opcua_variant_t *value) {
    if (!server || !node_id || !value)
        return false;

    opcua_node_t *node = find_node(server, node_id);
    if (!node)
        return false;

    node->value.value = *value;
    node->value.status = OPCUA_STATUS_GOOD;
    return true;
}

bool opcua_server_read_value(const opcua_server_t *server, const opcua_nodeid_t *node_id,
                             opcua_datavalue_t *dest_value) {
    if (!server || !node_id || !dest_value)
        return false;

    opcua_node_t *node = find_node((opcua_server_t *)server, node_id);
    if (!node)
        return false;

    *dest_value = node->value;
    return true;
}

uint32_t opcua_server_process_read(const opcua_server_t *server, const opcua_nodeid_t *node_id,
                                   opcua_attribute_id_t attr_id, opcua_datavalue_t *out_value) {
    if (!server || !node_id || !out_value)
        return OPCUA_STATUS_BAD_UNEXPECTED_ERROR;

    opcua_node_t *node = find_node((opcua_server_t *)server, node_id);
    if (!node) {
        out_value->status = OPCUA_STATUS_BAD_NODE_ID_UNKNOWN;
        return OPCUA_STATUS_BAD_NODE_ID_UNKNOWN;
    }

    if (!(node->access_level & 0x01)) {
        out_value->status = OPCUA_STATUS_BAD_NOT_READABLE;
        return OPCUA_STATUS_BAD_NOT_READABLE;
    }

    switch (attr_id) {
    case OPCUA_ATTR_VALUE:
        *out_value = node->value;
        return OPCUA_STATUS_GOOD;

    case OPCUA_ATTR_NODE_ID:
        out_value->value.type = OPCUA_TYPE_UINT32;
        out_value->value.value.uint32_val = node->node_id.id.numeric;
        out_value->status = OPCUA_STATUS_GOOD;
        return OPCUA_STATUS_GOOD;

    case OPCUA_ATTR_DISPLAY_NAME:
        out_value->value.type = OPCUA_TYPE_STRING;
        snprintf(out_value->value.value.string_val, sizeof(out_value->value.value.string_val), "%s",
                 node->display_name.text);
        out_value->status = OPCUA_STATUS_GOOD;
        return OPCUA_STATUS_GOOD;

    default:
        out_value->status = OPCUA_STATUS_BAD_ATTRIBUTE_ID_INVALID;
        return OPCUA_STATUS_BAD_ATTRIBUTE_ID_INVALID;
    }
}

uint32_t opcua_server_process_write(opcua_server_t *server, const opcua_nodeid_t *node_id,
                                    opcua_attribute_id_t attr_id,
                                    const opcua_datavalue_t *in_value) {
    if (!server || !node_id || !in_value)
        return OPCUA_STATUS_BAD_UNEXPECTED_ERROR;

    opcua_node_t *node = find_node(server, node_id);
    if (!node) {
        return OPCUA_STATUS_BAD_NODE_ID_UNKNOWN;
    }

    if (!(node->access_level & 0x02)) {
        return OPCUA_STATUS_BAD_NOT_WRITABLE;
    }

    if (attr_id != OPCUA_ATTR_VALUE) {
        return OPCUA_STATUS_BAD_ATTRIBUTE_ID_INVALID;
    }

    node->value = *in_value;
    return OPCUA_STATUS_GOOD;
}

size_t opcua_server_process_tcp_message(opcua_server_t *server, const uint8_t *req_data,
                                        size_t req_len, uint8_t *resp_data, size_t max_resp_len) {
    if (!server || !req_data || req_len < 8 || !resp_data || max_resp_len < 8) {
        return 0;
    }

    opcua_tcp_header_t hdr;
    if (!opcua_decode_tcp_header(req_data, req_len, &hdr) || hdr.msg_size != req_len ||
        hdr.is_final != 'F')
        return 0;

    if (memcmp(hdr.msg_type, "HEL", 3) == 0) {
        /* Hello メッセージ -> ACK 応答 */
        opcua_hello_message_t hello;
        memset(&hello, 0, sizeof(hello));
        if (!opcua_decode_hello(req_data, req_len, &hello))
            return 0;

        opcua_ack_message_t ack;
        memset(&ack, 0, sizeof(ack));
        ack.protocol_version = hello.protocol_version;
        ack.receive_buffer_size = 65535;
        ack.send_buffer_size = 65535;
        ack.max_message_size = OPCUA_MAX_MESSAGE_SIZE;
        ack.max_chunk_count = 1;

        return opcua_encode_ack(&ack, resp_data, max_resp_len);
    } else if (memcmp(hdr.msg_type, "MSG", 3) == 0) {
        /* MSG (Read / Write サービス等) */
        // 単純化されたReadサービス応答を生成
        opcua_tcp_header_t resp_hdr;
        memcpy(resp_hdr.msg_type, "MSG", 3);
        resp_hdr.is_final = 'F';

        size_t off = 8;
        // 先頭ノードの値を読み出してパック
        if (server->node_count > 0) {
            size_t n = opcua_encode_datavalue(&server->nodes[0].value, &resp_data[off],
                                              max_resp_len - off);
            if (!n)
                return 0;
            off += n;
        }
        resp_hdr.msg_size = (uint32_t)off;
        opcua_encode_tcp_header(&resp_hdr, resp_data, max_resp_len);
        return off;
    }

    return 0;
}
