#pragma once
#ifndef EMBED_CANOPEN_HPP
#define EMBED_CANOPEN_HPP

#include "canopen_types.h"
#include "canopen_od.h"
#include "canopen_node.h"
#include <initializer_list>
#include <string>
#include <cstring>
#include <type_traits>

namespace embed_canopen {

/** C++ の型から OD のデータ型を決める (対応しない型はコンパイルエラー) */
template <typename T> constexpr canopen_od_type_t od_type_of() {
    if constexpr (std::is_same_v<T, bool>) return OD_TYPE_BOOLEAN;
    else if constexpr (std::is_same_v<T, int8_t>) return OD_TYPE_INT8;
    else if constexpr (std::is_same_v<T, int16_t>) return OD_TYPE_INT16;
    else if constexpr (std::is_same_v<T, int32_t>) return OD_TYPE_INT32;
    else if constexpr (std::is_same_v<T, int64_t>) return OD_TYPE_INT64;
    else if constexpr (std::is_same_v<T, uint8_t>) return OD_TYPE_UINT8;
    else if constexpr (std::is_same_v<T, uint16_t>) return OD_TYPE_UINT16;
    else if constexpr (std::is_same_v<T, uint32_t>) return OD_TYPE_UINT32;
    else if constexpr (std::is_same_v<T, uint64_t>) return OD_TYPE_UINT64;
    else if constexpr (std::is_same_v<T, float>) return OD_TYPE_REAL32;
    else if constexpr (std::is_same_v<T, double>) return OD_TYPE_REAL64;
    else static_assert(sizeof(T) == 0, "unsupported CANopen object type");
}

/**
 * canopen_node_t の C++ ラッパー。OD は登録した変数を直接指すため、変数はノードより長生きさせること。
 * コールバックは C API (canopen_node_set_callbacks) を raw_handle() に対して使う。
 */
class Node {
public:
    explicit Node(uint8_t node_id = 1, const std::string &device_name = "CANopen-Node") {
        canopen_node_init(&node_, node_id, device_name.c_str());
    }
    Node(const Node &) = delete;            // OD が node_ 内部 (device_name 等) を指すため
    Node &operator=(const Node &) = delete;

    template <typename T>
    bool add_variable(uint16_t index, uint8_t subindex, T &var, canopen_od_access_t access = OD_ACCESS_RW) {
        return canopen_od_add_entry(&node_.od, index, subindex, od_type_of<T>(), access, &var, sizeof(T));
    }

    /** 文字列 (VISIBLE_STRING)。buffer の長さ全体を格納領域にする */
    template <size_t N>
    bool add_string(uint16_t index, uint8_t subindex, char (&buffer)[N], canopen_od_access_t access = OD_ACCESS_RO) {
        return canopen_od_add_entry(&node_.od, index, subindex, OD_TYPE_VISIBLE_STRING, access, buffer, N);
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

    bool pop_tx(can_frame_t &tx) { return canopen_node_pop_tx(&node_, &tx); }

    /** 受信フレームを処理し、生成されたフレームをすべて send(const can_frame_t&) へ渡す */
    template <typename Send> void handle(const can_frame_t &rx, Send &&send) {
        can_frame_t tx{};
        if (canopen_node_process_frame(&node_, &rx, &tx)) {
            send(tx);
            while (canopen_node_pop_tx(&node_, &tx))
                send(tx);
        }
    }

    /** 時間を進め、生成されたフレームをすべて send へ渡す */
    template <typename Send> void tick(uint32_t elapsed_ms, Send &&send) {
        can_frame_t tx{};
        if (canopen_node_timer_tick(&node_, elapsed_ms, &tx)) {
            send(tx);
            while (canopen_node_pop_tx(&node_, &tx))
                send(tx);
        }
    }

    bool send_tpdo1(const uint8_t *data, uint8_t len, can_frame_t &tx) {
        return canopen_node_send_tpdo1(&node_, data, len, &tx);
    }

    bool config_tpdo(uint8_t pdo_num, std::initializer_list<canopen_pdo_map_entry_t> map,
                     uint8_t transmission_type = 254, uint16_t event_timer_ms = 0) {
        return canopen_node_config_tpdo(&node_, pdo_num, map.begin(), static_cast<uint8_t>(map.size()),
                                        transmission_type, event_timer_ms);
    }

    bool config_rpdo(uint8_t pdo_num, std::initializer_list<canopen_pdo_map_entry_t> map,
                     uint8_t transmission_type = 254) {
        return canopen_node_config_rpdo(&node_, pdo_num, map.begin(), static_cast<uint8_t>(map.size()),
                                        transmission_type);
    }

    bool trigger_tpdo(uint8_t pdo_num) { return canopen_node_trigger_tpdo(&node_, pdo_num); }

    bool emcy(uint16_t error_code, uint8_t err_reg_bits = 0, const uint8_t *manufacturer = nullptr) {
        return canopen_node_emcy(&node_, error_code, err_reg_bits, manufacturer);
    }

    bool add_heartbeat_consumer(uint8_t remote_id, uint16_t timeout_ms) {
        return canopen_node_add_heartbeat_consumer(&node_, remote_id, timeout_ms);
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
