#include <iostream>
#include <vector>
#include <string>
#include <cstdio>
#include <chrono>

#include "embed_log/embed_log.h"
#include "embed_log/embed_log.hpp"

// 仮想Flashメモリ/SDカード保存用バッファ
static std::vector<std::string> s_flash_log_storage;

static void flash_storage_backend(const char *str, size_t, void *) {
    s_flash_log_storage.emplace_back(str);
}

static uint32_t get_system_uptime_ms() {
    static auto start_time = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    return (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count();
}

int main() {
    std::cout << "=====================================================" << std::endl;
    std::cout << " Embedded Logger (embed_log) Demonstration" << std::endl;
    std::cout << "=====================================================" << std::endl;

    // 1. ロガー初期化
    embed_log_init();
    embed_log_set_timestamp_fn(get_system_uptime_ms);
    embed_log_set_color_enabled(true);
    embed_log_set_level(EMBED_LOG_LEVEL_TRACE);

    // 2. セカンダリバックエンド（Flash保存）の追加
    embed_log_add_backend(flash_storage_backend, nullptr);

    // 3. 各種レベルのログ出力
    std::cout << "\n[1] Multi-level Colored Logging:\n";
    LOG_F("POWER", "Critical power failure detected! Voltage: %.2f V", 2.85f);
    LOG_E("NETWORK", "Connection timeout to server (IP: 192.168.1.100)");
    LOG_W("THERMAL", "CPU temperature high: %d C (Threshold: %d C)", 78, 75);
    LOG_I("SYSTEM", "System initialized successfully. Clock: 168 MHz");
    LOG_D("SENSOR", "Raw ADC value: 0x%04X, Converted: %.3f mV", 0x07A2, 1234.5f);
    LOG_T("BLE", "State transitioned: ADVERTISING -> CONNECTED");

    // 4. C++ ストリームロギング
    std::cout << "\n[2] C++ Stream Style Logging:\n";
    CPP_LOG_I("CPP_MOD") << "Motor RPM: " << 3500 << ", Target: " << 4000 << ", Duty: " << 87.5 << "%";

    // 5. バイナリ・メモリダンプ (Hex Dump)
    std::cout << "\n[3] Binary HexDump Output:\n";
    uint8_t packet_sample[] = {
        0xAA, 0x01, 0x05, 0x00, 0x0C,
        'H', 'e', 'l', 'l', 'o', ' ', 'E', 'm', 'b', 'e', 'd', '!',
        0x29, 0xB1, 0x55
    };
    LOG_HEXDUMP("UART_RX", packet_sample, sizeof(packet_sample));

    // 6. 仮想Flashストレージの記録確認
    std::cout << "\n[4] Verified Dual Backend (Flash Storage Captured Logs: "
              << s_flash_log_storage.size() << " entries)\n";

    std::cout << "\n=====================================================" << std::endl;
    std::cout << " embed_log demo completed successfully!" << std::endl;
    std::cout << "=====================================================" << std::endl;
    return 0;
}
