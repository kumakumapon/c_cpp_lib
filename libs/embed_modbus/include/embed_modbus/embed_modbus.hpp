#pragma once
#include "modbus_types.h"
#include "modbus_crc.h"
#include "modbus_slave.h"
#include "modbus_master.h"
#include "modbus_rtu.h"

#include <vector>
#include <functional>
#include <map>
#include <cstring>

namespace embed_modbus {

/**
 * @brief メモリバッファを直接マッピングできるシンプルなC++ Modbusスレーブクラス
 */
class Slave {
public:
    explicit Slave(uint8_t unit_id) : unit_id_(unit_id) {
        callbacks_.read_coils = &on_read_coils;
        callbacks_.read_discrete_inputs = &on_read_discrete;
        callbacks_.read_holding_registers = &on_read_holding;
        callbacks_.read_input_registers = &on_read_input;
        callbacks_.write_coils = &on_write_coils;
        callbacks_.write_holding_registers = &on_write_holding;

        modbus_slave_init(&slave_, unit_id_, &callbacks_, this);
    }

    void set_holding_registers(const std::map<uint16_t, uint16_t> &regs) {
        holding_regs_ = regs;
    }

    void set_holding_register(uint16_t addr, uint16_t val) {
        holding_regs_[addr] = val;
    }

    uint16_t get_holding_register(uint16_t addr) const {
        auto it = holding_regs_.find(addr);
        return (it != holding_regs_.end()) ? it->second : 0;
    }

    void set_coil(uint16_t addr, bool val) {
        coils_[addr] = val;
    }

    bool get_coil(uint16_t addr) const {
        auto it = coils_.find(addr);
        return (it != coils_.end()) ? it->second : false;
    }

    bool handle_rtu_request(const std::vector<uint8_t> &request, std::vector<uint8_t> &response) {
        uint8_t resp_buf[MODBUS_MAX_ADU_LENGTH];
        size_t resp_len = 0;

        bool has_resp = modbus_rtu_slave_handle(&slave_, request.data(), request.size(), resp_buf, &resp_len);
        if (has_resp && resp_len > 0) {
            response.assign(resp_buf, resp_buf + resp_len);
        } else {
            response.clear();
        }
        return has_resp;
    }

    const modbus_slave_t *raw_handle() const { return &slave_; }
    modbus_slave_t *raw_handle() { return &slave_; }

private:
    static modbus_exception_t on_read_coils(uint16_t addr, uint16_t count, uint8_t *dest, void *ud) {
        auto *self = static_cast<Slave *>(ud);
        for (uint16_t i = 0; i < count; ++i) {
            bool val = self->get_coil(addr + i);
            if (val) {
                dest[i / 8] |= (1 << (i % 8));
            } else {
                dest[i / 8] &= ~(1 << (i % 8));
            }
        }
        return MODBUS_EX_NONE;
    }

    static modbus_exception_t on_read_discrete(uint16_t addr, uint16_t count, uint8_t *dest, void *ud) {
        return on_read_coils(addr, count, dest, ud);
    }

    static modbus_exception_t on_read_holding(uint16_t addr, uint16_t count, uint16_t *dest, void *ud) {
        auto *self = static_cast<Slave *>(ud);
        for (uint16_t i = 0; i < count; ++i) {
            dest[i] = self->get_holding_register(addr + i);
        }
        return MODBUS_EX_NONE;
    }

    static modbus_exception_t on_read_input(uint16_t addr, uint16_t count, uint16_t *dest, void *ud) {
        return on_read_holding(addr, count, dest, ud);
    }

    static modbus_exception_t on_write_coils(uint16_t addr, uint16_t count, const uint8_t *src, void *ud) {
        auto *self = static_cast<Slave *>(ud);
        for (uint16_t i = 0; i < count; ++i) {
            bool val = (src[i / 8] & (1 << (i % 8))) != 0;
            self->set_coil(addr + i, val);
        }
        return MODBUS_EX_NONE;
    }

    static modbus_exception_t on_write_holding(uint16_t addr, uint16_t count, const uint16_t *src, void *ud) {
        auto *self = static_cast<Slave *>(ud);
        for (uint16_t i = 0; i < count; ++i) {
            self->set_holding_register(addr + i, src[i]);
        }
        return MODBUS_EX_NONE;
    }

    uint8_t unit_id_;
    modbus_slave_t slave_{};
    modbus_slave_callbacks_t callbacks_{};
    std::map<uint16_t, uint16_t> holding_regs_;
    std::map<uint16_t, bool> coils_;
};

} // namespace embed_modbus
