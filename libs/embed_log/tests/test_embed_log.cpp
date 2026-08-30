#include <iostream>
#include <vector>
#include <string>
#include <cassert>
#include <cstring>

#include "embed_log/embed_log.h"
#include "embed_log/embed_log.hpp"

static std::vector<std::string> s_captured_logs;
static uint32_t s_mock_time_ms = 1000;

static void mock_log_backend(const char *str, size_t, void *) {
    s_captured_logs.emplace_back(str);
}

static uint32_t mock_get_time() {
    return s_mock_time_ms;
}

void test_log_level_filtering() {
    std::cout << "[Test] Running log level filtering tests..." << std::endl;
    embed_log_init();
    embed_log_clear_backends();
    embed_log_add_backend(mock_log_backend, nullptr);
    embed_log_set_color_enabled(false);
    embed_log_set_timestamp_fn(mock_get_time);

    s_captured_logs.clear();
    embed_log_set_level(EMBED_LOG_LEVEL_INFO);

    LOG_T("TAG", "Trace message");
    LOG_D("TAG", "Debug message");
    LOG_I("TAG", "Info message");
    LOG_W("TAG", "Warn message");
    LOG_E("TAG", "Error message");
    LOG_F("TAG", "Fatal message");

    // INFO以上（INFO, WARN, ERROR, FATAL）のみ4件記録される
    assert(s_captured_logs.size() == 4);
    assert(s_captured_logs[0].find("[INFO ][TAG] Info message") != std::string::npos);
    assert(s_captured_logs[1].find("[WARN ][TAG] Warn message") != std::string::npos);
    assert(s_captured_logs[2].find("[ERROR][TAG] Error message") != std::string::npos);
    assert(s_captured_logs[3].find("[FATAL][TAG] Fatal message") != std::string::npos);

    std::cout << "[Test] Log level filtering passed!" << std::endl;
}

void test_timestamp_and_tags() {
    std::cout << "[Test] Running timestamp and tag tests..." << std::endl;
    s_captured_logs.clear();
    s_mock_time_ms = 12345;

    LOG_I("SENSOR", "Temperature: %d C", 25);
    assert(s_captured_logs.size() == 1);
    assert(s_captured_logs[0].find("[00012345]") != std::string::npos);
    assert(s_captured_logs[0].find("[SENSOR]") != std::string::npos);
    assert(s_captured_logs[0].find("Temperature: 25 C") != std::string::npos);

    std::cout << "[Test] Timestamp and tags passed!" << std::endl;
}

void test_hexdump() {
    std::cout << "[Test] Running hexdump tests..." << std::endl;
    s_captured_logs.clear();
    embed_log_set_level(EMBED_LOG_LEVEL_DEBUG);

    uint8_t buffer[10] = {0x01, 0x02, 0x03, 0x04, 'A', 'B', 'C', 0xAA, 0xBB, 0xCC};
    LOG_HEXDUMP("DUMP_TAG", buffer, sizeof(buffer));

    assert(!s_captured_logs.empty());
    bool found_hex = false;
    for (const auto &line : s_captured_logs) {
        if (line.find("01 02 03 04 41 42 43 AA BB CC") != std::string::npos) {
            found_hex = true;
            break;
        }
    }
    assert(found_hex);
    std::cout << "[Test] Hexdump passed!" << std::endl;
}

void test_cpp_stream_logging() {
    std::cout << "[Test] Running C++ stream logging tests..." << std::endl;
    s_captured_logs.clear();

    CPP_LOG_I("CPP_TAG") << "Value: " << 42 << ", Float: " << 3.14;
    assert(s_captured_logs.size() == 1);
    assert(s_captured_logs[0].find("Value: 42, Float: 3.14") != std::string::npos);

    std::cout << "[Test] C++ stream logging passed!" << std::endl;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << " Running embed_log test suite..." << std::endl;
    std::cout << "========================================" << std::endl;

    test_log_level_filtering();
    test_timestamp_and_tags();
    test_hexdump();
    test_cpp_stream_logging();

    std::cout << "========================================" << std::endl;
    std::cout << " All embed_log tests PASSED successfully!" << std::endl;
    std::cout << "========================================" << std::endl;
    return 0;
}
