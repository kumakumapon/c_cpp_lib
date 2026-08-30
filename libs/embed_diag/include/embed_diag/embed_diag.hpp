#pragma once
#ifndef EMBED_DIAG_HPP
#define EMBED_DIAG_HPP

#include "embed_diag.h"
#include <string>
#include <vector>

namespace embed_diag {

class StackMonitor {
public:
    StackMonitor(void *stack_buffer, size_t stack_size) {
        diag_stack_init(&mon_, stack_buffer, stack_size);
    }

    bool check(size_t &free_bytes, size_t &used_bytes) {
        return diag_stack_check(&mon_, &free_bytes, &used_bytes);
    }

    size_t high_water_mark() const { return mon_.high_water_mark; }

private:
    diag_stack_monitor_t mon_{};
};

class CpuMonitor {
public:
    CpuMonitor() {
        diag_cpu_init(&mon_);
    }

    void update(uint64_t active_us, uint64_t total_us) {
        diag_cpu_update(&mon_, active_us, total_us);
    }

    float usage_percent() const {
        return diag_cpu_get_usage(&mon_);
    }

private:
    diag_cpu_monitor_t mon_{};
};

class Recorder {
public:
    Recorder() {
        diag_recorder_init(&recorder_);
    }

    void log(diag_log_level_t level, uint16_t code, const std::string &msg, uint32_t timestamp_ms = 0) {
        diag_recorder_log(&recorder_, level, code, msg.c_str(), timestamp_ms);
    }

    void record_fault(const diag_fault_context_t &ctx) {
        diag_recorder_record_fault(&recorder_, &ctx);
    }

    std::vector<diag_log_entry_t> get_logs() const {
        std::vector<diag_log_entry_t> list(recorder_.count);
        if (!list.empty()) {
            size_t n = diag_recorder_get_logs(&recorder_, list.data(), list.size());
            list.resize(n);
        }
        return list;
    }

    bool has_crashed() const { return recorder_.has_crashed; }
    const diag_fault_context_t &last_fault() const { return recorder_.last_fault; }

private:
    diag_recorder_t recorder_{};
};

} // namespace embed_diag

#endif /* EMBED_DIAG_HPP */
