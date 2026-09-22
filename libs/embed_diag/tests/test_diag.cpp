#include <iostream>
#include "embed_test/embed_test.hpp"
#include <vector>
#include <cmath>
#include <cstring>

#include "embed_diag/embed_diag.h"
#include "embed_diag/embed_diag.hpp"

TEST_CASE(test_stack_monitor) {
    std::cout << "# [Test] Running Stack Monitor tests..." << std::endl;
    uint8_t dummy_stack[256];
    embed_diag::StackMonitor monitor(dummy_stack, sizeof(dummy_stack));

    size_t free_b = 0, used_b = 0;
    CHECK(monitor.check(free_b, used_b));
    CHECK(used_b == 0);
    CHECK(free_b == 256);

    // スタックトップ側（末尾側）から 100 バイト使用
    memset(&dummy_stack[256 - 100], 0x00, 100);
    CHECK(monitor.check(free_b, used_b));
    CHECK(used_b == 100);
    CHECK(free_b == 156);

    // スタックをオーバーフローさせる (底部 0〜15 も破壊)
    memset(dummy_stack, 0x00, 256);
    CHECK(!monitor.check(free_b, used_b)); // カナリア破壊検知

    std::cout << "# [Test] Stack Monitor passed!" << std::endl;
}

TEST_CASE(test_cpu_monitor) {
    std::cout << "# [Test] Running CPU Monitor tests..." << std::endl;
    embed_diag::CpuMonitor cpu;

    // 100ms 中 25ms アクティブ -> 25%
    cpu.update(25000, 100000);
    CHECK(fabsf(cpu.usage_percent() - 25.0f) < 0.1f);

    std::cout << "# [Test] CPU Monitor passed!" << std::endl;
}

TEST_CASE(test_recorder) {
    std::cout << "# [Test] Running Recorder tests..." << std::endl;
    embed_diag::Recorder rec;

    rec.log(DIAG_LEVEL_INFO, 0x0001, "System Started", 100);
    rec.log(DIAG_LEVEL_WARN, 0x0002, "High Temp Detected", 250);
    rec.log(DIAG_LEVEL_ERROR, 0x00E1, "Motor Overcurrent", 300);

    auto logs = rec.get_logs();
    CHECK(logs.size() == 3);
    CHECK(logs[0].error_code == 0x0001);
    CHECK(strcmp(logs[0].message, "System Started") == 0);
    CHECK(logs[2].error_code == 0x00E1);

    diag_fault_context_t fault{};
    fault.pc = 0x08001234;
    fault.lr = 0x08000567;
    fault.cfsr = 0x00010000;
    rec.record_fault(fault);

    CHECK(rec.has_crashed());
    CHECK(rec.last_fault().pc == 0x08001234);

    std::cout << "# [Test] Recorder passed!" << std::endl;
}

EMBED_TEST_MAIN()
