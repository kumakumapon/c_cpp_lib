#pragma once
#include "eip_types.h"
#include "eip_encap.h"
#include "eip_cip.h"
#include "eip_adapter.h"

#include <string>
#include <vector>
#include <cstring>

namespace embed_eip {

class Adapter {
public:
    Adapter(uint16_t vendor_id, uint16_t device_type, uint16_t product_code,
            const std::string &product_name = "Embedded CIP Adapter") {
        cip_identity_attr_t id;
        memset(&id, 0, sizeof(id));
        id.vendor_id = vendor_id;
        id.device_type = device_type;
        id.product_code = product_code;
        id.major_revision = 1;
        id.minor_revision = 0;
        id.status = 0x0060; // Configured, Owned
        id.serial_number = 0x12345678;
        strncpy(id.product_name, product_name.c_str(), sizeof(id.product_name) - 1);

        eip_adapter_init(&adapter_, &id);
    }

    bool add_assembly(uint16_t instance_id, size_t size) {
        return eip_adapter_add_assembly(&adapter_, instance_id, size);
    }

    bool set_assembly_data(uint16_t instance_id, const std::vector<uint8_t> &data) {
        return eip_adapter_set_assembly_data(&adapter_, instance_id, data.data(), data.size());
    }

    template <typename T>
    bool set_assembly_pod(uint16_t instance_id, const T &val) {
        return eip_adapter_set_assembly_data(&adapter_, instance_id, &val, sizeof(T));
    }

    bool get_assembly_data(uint16_t instance_id, std::vector<uint8_t> &dest) const {
        uint8_t buf[EIP_MAX_ASSEMBLY_SIZE];
        size_t sz = 0;
        if (eip_adapter_get_assembly_data(&adapter_, instance_id, buf, &sz)) {
            dest.assign(buf, buf + sz);
            return true;
        }
        return false;
    }

    template <typename T>
    bool get_assembly_pod(uint16_t instance_id, T &val) const {
        size_t sz = 0;
        return eip_adapter_get_assembly_data(&adapter_, instance_id, &val, &sz) && sz == sizeof(T);
    }

    std::vector<uint8_t> process_packet(const std::vector<uint8_t> &request) {
        uint8_t resp_buf[EIP_MAX_PACKET_SIZE];
        size_t len = eip_adapter_process_packet(&adapter_, request.data(), request.size(), resp_buf, sizeof(resp_buf));
        return std::vector<uint8_t>(resp_buf, resp_buf + len);
    }

    eip_adapter_t *raw_handle() { return &adapter_; }
    const eip_adapter_t *raw_handle() const { return &adapter_; }

private:
    eip_adapter_t adapter_{};
};

} // namespace embed_eip
