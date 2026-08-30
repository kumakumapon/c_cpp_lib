#pragma once
#ifndef EMBED_CANOPEN_HPP
#define EMBED_CANOPEN_HPP

#include "canopen_types.h"
#include "canopen_od.h"
#include "canopen_node.h"
#include <string>
#include <cstring>
#include <type_traits>

namespace embed_canopen {

class Node {
public:
    explicit Node(uint8_t node_id = 1, const std::string &device_name = "CANopen-Node") {
        canopen_node_init(&node_, node_id, device_name.c_str());
    }

    template <typename T>
    bool add_variable(uint16_t index, uint8_t subindex, T &var, canopen_od_access_t access = OD_ACCESS_RW) {
        canopen_od_type_t type = OD_TYPE_UINT32;
        if constexpr (std::is_same_v<T, bool>) type = OD_TYPE_BOOLEAN;
        else if constexpr (std::is_same_v<T, int8_t>) type = OD_TYPE_INT8;
        else if constexpr (std::is_same_v<T, int16_t>) type = OD_TYPE_INT16;
        else if constexpr (std::is_same_v<T, int32_t>) type = OD_TYPE_INT32;
        else if constexpr (std::is_same_v<T, uint8_t>) type = OD_TYPE_UINT8;
        else if constexpr (std::is_same_v<T, uint16_t>) type = OD_TYPE_UINT16;
        else if constexpr (std::is_same_v<T, uint32_t>) type = OD_TYPE_UINT32;
        else if constexpr (std::is_same_v<T, float>) type = OD_TYPE_REAL32;

        return canopen_od_add_entry(&node_.od, index, subindex, type, access, &var, sizeof(T));
    }

    template <typename T>
    bool read_od(uint16_t index, uint8_t subindex, T &out_val) const {
        size_t len = 0;
        return canopen_od_read(&node_.od, index, subindex, &out_val, sizeof(T), &len);
    }

    template <typename T>
    bool write_od(uint16_t index, uint8_t subindex, const T &val) {
        return canopen_od_write(&node_.od, index, subindex, &val, sizeof(T));
    }

    bool process_frame(const can_frame_t &rx, can_frame_t &tx) {
        return canopen_node_process_frame(&node_, &rx, &tx);
    }

    bool timer_tick(uint32_t elapsed_ms, can_frame_t &tx) {
        return canopen_node_timer_tick(&node_, elapsed_ms, &tx);
    }

    bool send_tpdo1(const uint8_t *data, uint8_t len, can_frame_t &tx) {
        return canopen_node_send_tpdo1(&node_, data, len, &tx);
    }

    canopen_nmt_state_t nmt_state() const { return node_.nmt_state; }
    uint8_t node_id() const { return node_.node_id; }
    canopen_node_t* raw_handle() { return &node_; }
    const canopen_node_t* raw_handle() const { return &node_; }

private:
    canopen_node_t node_{};
};

} // namespace embed_canopen

#endif /* EMBED_CANOPEN_HPP */
