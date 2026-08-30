#include <iostream>
#include <cassert>
#include <vector>
#include <cmath>
#include <cstring>

#include "embed_diag/embed_diag.h"
#include "embed_diag/embed_diag.hpp"

void test_stack_monitor() {
    std::cout << "[Test] Running Stack Monitor tests..." << std::endl;
    uint8_t dummy_stack[256];
    embed_diag::StackMonitor monitor(dummy_stack, sizeof(dummy_stack));

    size_t free_b = 0, used_b = 0;
    assert(monitor.check(free_b, used_b));
    assert(used_b == 0);
    assert(free_b == 256);

    // スタックトップ側（末尾側）から 100 バイト使用
    memset(&dummy_stack[256 - 100], 0x00, 100);
    assert(monitor.check(free_b, used_b));
    assert(used_b == 100);
    assert(free_b == 156);

    // スタックをオーバーフローさせる (底部 0〜15 も破壊)
    memset(dummy_stack, 0x00, 256);
    assert(!monitor.check(free_b, used_b)); // カナリア破壊検知

    std::cout << "[Test] Stack Monitor passed!" << std::endl;
}

void test_cpu_monitor() {
    std::cout << "[Test] Running CPU Monitor tests..." << std::endl;
    embed_diag::CpuMonitor cpu;

    // 100ms 中 25ms アクティブ -> 25%
    cpu.update(25000, 100000);
    assert(fabsf(cpu.usage_percent() - 25.0f) < 0.1f);

    std::cout << "[Test] CPU Monitor passed!" << std::endl;
}

void test_recorder() {
    std::cout << "[Test] Running Recorder tests..." << std::endl;
    embed_diag::Recorder rec;

    rec.log(DIAG_LEVEL_INFO, 0x0001, "System Started", 100);
    rec.log(DIAG_LEVEL_WARN, 0x0002, "High Temp Detected", 250);
    rec.log(DIAG_LEVEL_ERROR, 0x00E1, "Motor Overcurrent", 300);

    auto logs = rec.get_logs();
    assert(logs.size() == 3);
    assert(logs[0].error_code == 0x0001);
    assert(strcmp(logs[0].message, "System Started") == 0);
    assert(logs[2].error_code == 0x00E1);

    diag_fault_context_t fault{};
    fault.pc = 0x08001234;
    fault.lr = 0x08000567;
    fault.cfsr = 0x00010000;
    rec.record_fault(fault);

    assert(rec.has_crashed());
    assert(rec.last_fault().pc == 0x08001234);

    std::cout << "[Test] Recorder passed!" << std::endl;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << " Running embed_diag test suite..." << std::endl;
    std::cout << "========================================" << std::endl;

    test_stack_monitor();
    test_cpu_monitor();
    test_recorder();

    std::cout << "========================================" << std::endl;
    std::cout << " All embed_diag tests PASSED!" << std::endl;
    std::cout << "========================================" << std::endl;
    return 0;
}
