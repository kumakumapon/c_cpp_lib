#pragma once
#include "opcua_types.h"
#include "opcua_binary.h"
#include "opcua_server.h"

#include <string>
#include <vector>
#include <cstring>

namespace embed_opcua {

class Server {
public:
    explicit Server(const std::string &endpoint_url = "opc.tcp://localhost:4840") {
        opcua_server_init(&server_, endpoint_url.c_str());
    }

    bool add_variable(uint16_t ns, uint32_t id, const std::string &name, double value, uint8_t access = 0x03) {
        opcua_nodeid_t nid{};
        nid.namespace_index = ns;
        nid.id_type = OPCUA_NODEID_NUMERIC;
        nid.id.numeric = id;

        opcua_variant_t v{};
        v.type = OPCUA_TYPE_DOUBLE;
        v.value.double_val = value;

        return opcua_server_add_variable(&server_, &nid, name.c_str(), &v, access);
    }

    bool add_variable(uint16_t ns, uint32_t id, const std::string &name, int32_t value, uint8_t access = 0x03) {
        opcua_nodeid_t nid{};
        nid.namespace_index = ns;
        nid.id_type = OPCUA_NODEID_NUMERIC;
        nid.id.numeric = id;

        opcua_variant_t v{};
        v.type = OPCUA_TYPE_INT32;
        v.value.int32_val = value;

        return opcua_server_add_variable(&server_, &nid, name.c_str(), &v, access);
    }

    bool add_variable(uint16_t ns, uint32_t id, const std::string &name, bool value, uint8_t access = 0x03) {
        opcua_nodeid_t nid{};
        nid.namespace_index = ns;
        nid.id_type = OPCUA_NODEID_NUMERIC;
        nid.id.numeric = id;

        opcua_variant_t v{};
        v.type = OPCUA_TYPE_BOOLEAN;
        v.value.boolean = value;

        return opcua_server_add_variable(&server_, &nid, name.c_str(), &v, access);
    }

    bool add_variable(uint16_t ns, uint32_t id, const std::string &name, const std::string &value, uint8_t access = 0x03) {
        opcua_nodeid_t nid{};
        nid.namespace_index = ns;
        nid.id_type = OPCUA_NODEID_NUMERIC;
        nid.id.numeric = id;

        opcua_variant_t v{};
        v.type = OPCUA_TYPE_STRING;
        strncpy(v.value.string_val, value.c_str(), sizeof(v.value.string_val) - 1);

        return opcua_server_add_variable(&server_, &nid, name.c_str(), &v, access);
    }

    bool set_value(uint16_t ns, uint32_t id, double value) {
        opcua_nodeid_t nid{};
        nid.namespace_index = ns;
        nid.id_type = OPCUA_NODEID_NUMERIC;
        nid.id.numeric = id;

        opcua_variant_t v{};
        v.type = OPCUA_TYPE_DOUBLE;
        v.value.double_val = value;
        return opcua_server_write_value(&server_, &nid, &v);
    }

    bool set_value(uint16_t ns, uint32_t id, int32_t value) {
        opcua_nodeid_t nid{};
        nid.namespace_index = ns;
        nid.id_type = OPCUA_NODEID_NUMERIC;
        nid.id.numeric = id;

        opcua_variant_t v{};
        v.type = OPCUA_TYPE_INT32;
        v.value.int32_val = value;
        return opcua_server_write_value(&server_, &nid, &v);
    }

    bool get_value(uint16_t ns, uint32_t id, opcua_datavalue_t &dest) const {
        opcua_nodeid_t nid{};
        nid.namespace_index = ns;
        nid.id_type = OPCUA_NODEID_NUMERIC;
        nid.id.numeric = id;
        return opcua_server_read_value(&server_, &nid, &dest);
    }

    std::vector<uint8_t> process_message(const std::vector<uint8_t> &request) {
        uint8_t resp_buf[OPCUA_MAX_MESSAGE_SIZE];
        size_t len = opcua_server_process_tcp_message(&server_, request.data(), request.size(), resp_buf, sizeof(resp_buf));
        return std::vector<uint8_t>(resp_buf, resp_buf + len);
    }

    opcua_server_t *raw_handle() { return &server_; }
    const opcua_server_t *raw_handle() const { return &server_; }

private:
    opcua_server_t server_{};
};

} // namespace embed_opcua
