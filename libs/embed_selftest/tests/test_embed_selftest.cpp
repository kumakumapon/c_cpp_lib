#include "embed_selftest/embed_selftest.h"
#include "embed_test/embed_test.hpp"
static void feed(void *p) { ++*(unsigned *)p; }
static bool cpu(void *) { return true; }
TEST_CASE(memory_clock_and_watchdog) {
    uint32_t ram[64];
    CHECK(embed_selftest_march_c(ram, 64));
    for (auto x : ram)
        CHECK_EQ(x, 0u);
    CHECK(!embed_selftest_march_c(nullptr, 64));
    CHECK(embed_selftest_rom("123456789", 9, 0xcbf43926));
    CHECK(!embed_selftest_rom("123456780", 9, 0xcbf43926));
    uint8_t stack[32];
    embed_selftest_stack_fill(stack, 32, 0xa5);
    stack[24] = 0;
    CHECK_EQ(embed_selftest_stack_unused(stack, 32, 0xa5, true), 24u);
    CHECK_EQ(embed_selftest_stack_unused(stack, 32, 0xa5, false), 7u);
    CHECK(embed_selftest_cpu(cpu, nullptr));
    CHECK(!embed_selftest_cpu(nullptr, nullptr));
    CHECK(embed_selftest_clock(101, 100, 10000));
    CHECK(!embed_selftest_clock(102, 100, 10000));
    CHECK(embed_selftest_clock(UINT64_MAX, UINT64_MAX, 1000000));
    unsigned feeds = 0;
    embed_watchdog w{};
    CHECK(embed_watchdog_init(&w, 0, 5, 10, feed, &feeds));
    CHECK(!embed_watchdog_poll(&w, 4, true));
    CHECK(embed_watchdog_poll(&w, 5, true));
    CHECK_EQ(feeds, 1u);
    CHECK(!embed_watchdog_poll(&w, 16, true));
    CHECK(w.fault);
    CHECK(!embed_watchdog_poll(&w, 17, true));
}

EMBED_TEST_MAIN()
