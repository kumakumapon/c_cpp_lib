// スタック監視 (カナリア境界・ハイウォーターマーク)、CPU 使用率、障害ログのリング、
// フォールトコンテキストの境界と順序を固定するテスト。
#include "embed_test/embed_test.hpp"
#include <cstdint>
#include <cstring>
#include <string>

#include "embed_diag/embed_diag.h"

namespace {
constexpr size_t kSize = 64;

// スタックは下向きに伸びるので、使用済み (≠0xA5) は末尾 (高アドレス側) から n バイト
void use_top(uint8_t *stack, size_t size, size_t n) {
    for (size_t i = size - n; i < size; ++i) stack[i] = 0x11;
}
} // namespace

// ---- スタック ----
TEST_CASE(stack_init_fills_canary_and_sets_fields) {
    uint8_t stack[kSize];
    std::memset(stack, 0x00, sizeof(stack));
    diag_stack_monitor_t mon;
    std::memset(&mon, 0, sizeof(mon));
    diag_stack_init(&mon, stack, sizeof(stack));
    CHECK(mon.stack_start == stack);
    CHECK_EQ(mon.stack_size, kSize);
    CHECK_EQ(mon.canary_depth, 16u);
    CHECK_EQ(mon.high_water_mark, 0u);
    for (size_t i = 0; i < kSize; ++i) {
        if (stack[i] != 0xA5) {
            CHECK_EQ((int)stack[i], 0xA5);
            break;
        }
    }
    // 先頭・末尾ともに充填されている
    CHECK_EQ((int)stack[0], 0xA5);
    CHECK_EQ((int)stack[kSize - 1], 0xA5);
}

TEST_CASE(stack_init_rejects_bad_arguments) {
    uint8_t stack[kSize];
    std::memset(stack, 0x00, sizeof(stack));
    diag_stack_monitor_t mon;
    std::memset(&mon, 0, sizeof(mon));

    // 最小サイズ (16) 未満は拒否: 監視構造体もバッファも触らない
    diag_stack_init(&mon, stack, 15);
    CHECK(mon.stack_start == nullptr);
    CHECK_EQ((int)stack[0], 0);
    diag_stack_init(&mon, nullptr, kSize);
    CHECK(mon.stack_start == nullptr);
    diag_stack_init(nullptr, stack, kSize);
    CHECK_EQ((int)stack[0], 0);

    // ちょうど 16 は受け付け、全域が未使用なら正常
    diag_stack_init(&mon, stack, 16);
    CHECK(mon.stack_start == stack);
    CHECK_EQ(mon.stack_size, 16u);
    CHECK_EQ((int)stack[15], 0xA5);
    CHECK_EQ((int)stack[16], 0); // 範囲外は触らない
    size_t fr = 0, us = 99;
    CHECK(diag_stack_check(&mon, &fr, &us));
    CHECK_EQ(fr, 16u);
    CHECK_EQ(us, 0u);
}

TEST_CASE(stack_check_rejects_uninitialized_monitor) {
    diag_stack_monitor_t mon;
    std::memset(&mon, 0, sizeof(mon));
    CHECK(!diag_stack_check(&mon, nullptr, nullptr));
    CHECK(!diag_stack_check(nullptr, nullptr, nullptr));
}

TEST_CASE(stack_canary_boundary) {
    uint8_t stack[kSize];
    diag_stack_monitor_t mon;
    diag_stack_init(&mon, stack, sizeof(stack));
    size_t fr = 0, us = 0;

    // 未使用 (free) が 16 バイトあれば正常、15 バイトに食い込むとオーバーフロー
    use_top(stack, kSize, kSize - 16);
    CHECK(diag_stack_check(&mon, &fr, &us));
    CHECK_EQ(fr, 16u);
    CHECK_EQ(us, kSize - 16);

    use_top(stack, kSize, kSize - 15);
    CHECK(!diag_stack_check(&mon, &fr, &us));
    CHECK_EQ(fr, 15u);
    CHECK_EQ(us, kSize - 15);

    // 全域使用
    use_top(stack, kSize, kSize);
    CHECK(!diag_stack_check(&mon, &fr, &us));
    CHECK_EQ(fr, 0u);
    CHECK_EQ(us, kSize);
}

TEST_CASE(stack_untouched_is_all_free) {
    uint8_t stack[kSize];
    diag_stack_monitor_t mon;
    diag_stack_init(&mon, stack, sizeof(stack));
    size_t fr = 0, us = 99;
    CHECK(diag_stack_check(&mon, &fr, &us));
    CHECK_EQ(fr, kSize);
    CHECK_EQ(us, 0u);
    // NULL の出力先を許す
    CHECK(diag_stack_check(&mon, nullptr, nullptr));
    CHECK(diag_stack_check(&mon, &fr, nullptr));
    CHECK(diag_stack_check(&mon, nullptr, &us));
}

TEST_CASE(stack_high_water_mark) {
    uint8_t stack[kSize];
    diag_stack_monitor_t mon;
    diag_stack_init(&mon, stack, sizeof(stack));
    size_t fr = 0, us = 0;

    use_top(stack, kSize, 10);
    diag_stack_check(&mon, &fr, &us);
    CHECK_EQ(us, 10u);
    CHECK_EQ(mon.high_water_mark, 10u);

    // 増加: 更新される
    use_top(stack, kSize, 20);
    diag_stack_check(&mon, &fr, &us);
    CHECK_EQ(us, 20u);
    CHECK_EQ(fr, kSize - 20);

    // 同値: 変わらない
    diag_stack_check(&mon, &fr, &us);
    CHECK_EQ(us, 20u);

    // 減少 (使用領域をカナリアで埋め戻した): 最大値は保持、free は現在値
    std::memset(stack + kSize - 20, 0xA5, 15); // 使用域の下側 15 バイトが埋め戻された
    diag_stack_check(&mon, &fr, &us);
    CHECK_EQ(mon.high_water_mark, 20u);
    CHECK_EQ(us, 20u);
    CHECK_EQ(fr, kSize - 5); // 底から連続する 0xA5 が 59 バイトに延びる
}

TEST_CASE(stack_scan_stops_at_first_modified_byte) {
    // 底から数えて最初の非カナリアバイトまでが未使用。上位に残る 0xA5 は数えない
    uint8_t stack[kSize];
    diag_stack_monitor_t mon;
    diag_stack_init(&mon, stack, sizeof(stack));
    stack[20] = 0x00;
    size_t fr = 0, us = 0;
    diag_stack_check(&mon, &fr, &us);
    CHECK_EQ(fr, 20u);
    CHECK_EQ(us, kSize - 20);
}

TEST_CASE(stack_scan_does_not_read_past_the_end) {
    // 監視範囲の直後が 0xA5 でも、そこまで未使用として数えない
    uint8_t buf[kSize + 16];
    diag_stack_monitor_t mon;
    diag_stack_init(&mon, buf, kSize);
    std::memset(buf + kSize, 0xA5, 16);
    size_t fr = 0, us = 99;
    CHECK(diag_stack_check(&mon, &fr, &us));
    CHECK_EQ(fr, kSize);
    CHECK_EQ(us, 0u);
    CHECK_EQ(mon.high_water_mark, 0u);
}

// ---- CPU 使用率 ----
TEST_CASE(cpu_init_and_null_safety) {
    diag_cpu_monitor_t mon;
    std::memset(&mon, 0xFF, sizeof(mon));
    diag_cpu_init(&mon);
    CHECK_EQ(mon.total_time_us, 0u);
    CHECK_EQ(mon.active_time_us, 0u);
    CHECK_EQ(mon.last_calc_time_us, 0u);
    CHECK_NEAR(diag_cpu_get_usage(&mon), 0.0f, 0.0f);

    diag_cpu_init(nullptr);
    diag_cpu_update(nullptr, 1, 2);
    CHECK_NEAR(diag_cpu_get_usage(nullptr), 0.0f, 0.0f);
}

TEST_CASE(cpu_usage_values) {
    diag_cpu_monitor_t mon;
    diag_cpu_init(&mon);

    diag_cpu_update(&mon, 250, 1000);
    CHECK_NEAR(diag_cpu_get_usage(&mon), 25.0f, 1e-4f);
    diag_cpu_update(&mon, 0, 1000);
    CHECK_NEAR(diag_cpu_get_usage(&mon), 0.0f, 0.0f);
    diag_cpu_update(&mon, 1000, 1000);
    CHECK_NEAR(diag_cpu_get_usage(&mon), 100.0f, 1e-4f);
    diag_cpu_update(&mon, 1, 3);
    CHECK_NEAR(diag_cpu_get_usage(&mon), 100.0f / 3.0f, 1e-3f);
    diag_cpu_update(&mon, 1, 4);
    CHECK_NEAR(diag_cpu_get_usage(&mon), 25.0f, 1e-4f);
}

TEST_CASE(cpu_usage_clamps_at_100) {
    diag_cpu_monitor_t mon;
    diag_cpu_init(&mon);
    diag_cpu_update(&mon, 1500, 1000);
    CHECK_NEAR(diag_cpu_get_usage(&mon), 100.0f, 0.0f);
    // 表示値だけをクランプし、累積時間は与えた値のまま
    CHECK_EQ(mon.active_time_us, 1500u);
    CHECK_EQ(mon.total_time_us, 1000u);
}

TEST_CASE(cpu_total_zero_is_ignored) {
    diag_cpu_monitor_t mon;
    diag_cpu_init(&mon);
    diag_cpu_update(&mon, 500, 1000);
    diag_cpu_update(&mon, 100, 0); // 0 除算を避け、何も更新しない
    CHECK_NEAR(diag_cpu_get_usage(&mon), 50.0f, 1e-4f);
    CHECK_EQ(mon.active_time_us, 500u);
    CHECK_EQ(mon.total_time_us, 1000u);
}

TEST_CASE(cpu_accumulates_time_and_reports_last_interval) {
    diag_cpu_monitor_t mon;
    diag_cpu_init(&mon);
    diag_cpu_update(&mon, 100, 1000);
    diag_cpu_update(&mon, 300, 1000);
    diag_cpu_update(&mon, 50, 500);
    CHECK_EQ(mon.active_time_us, 450u);
    CHECK_EQ(mon.total_time_us, 2500u);
    // 表示値は直近区間 (50/500) の使用率で、累積平均 (18%) ではない
    CHECK_NEAR(diag_cpu_get_usage(&mon), 10.0f, 1e-4f);

    // 64 ビットの累積が 32 ビットで切れない
    diag_cpu_update(&mon, 4000000000ULL, 8000000000ULL);
    diag_cpu_update(&mon, 4000000000ULL, 8000000000ULL);
    CHECK_EQ(mon.total_time_us, 2500ULL + 16000000000ULL);
    CHECK_EQ(mon.active_time_us, 450ULL + 8000000000ULL);
    CHECK_NEAR(diag_cpu_get_usage(&mon), 50.0f, 1e-3f);
}

// ---- 障害ログ ----
namespace {
void log_n(diag_recorder_t *r, int first, int last) {
    for (int i = first; i <= last; ++i) {
        std::string m = "msg" + std::to_string(i);
        diag_recorder_log(r, (diag_log_level_t)(i % 4), (uint16_t)(100 + i), m.c_str(),
                          (uint32_t)(1000 + i));
    }
}
} // namespace

TEST_CASE(recorder_init_clears) {
    diag_recorder_t r;
    std::memset(&r, 0xFF, sizeof(r));
    diag_recorder_init(&r);
    CHECK_EQ(r.head, 0u);
    CHECK_EQ(r.count, 0u);
    CHECK(!r.has_crashed);
    diag_log_entry_t out[4];
    CHECK_EQ(diag_recorder_get_logs(&r, out, 4), 0u);
    diag_recorder_init(nullptr);
}

TEST_CASE(recorder_stores_fields) {
    diag_recorder_t r;
    diag_recorder_init(&r);
    diag_recorder_log(&r, DIAG_LEVEL_ERROR, 0xBEEF, "boom", 123456);
    CHECK_EQ(r.count, 1u);
    CHECK_EQ(r.head, 1u);
    diag_log_entry_t out[2];
    CHECK_EQ(diag_recorder_get_logs(&r, out, 2), 1u);
    CHECK_EQ(out[0].timestamp_ms, 123456u);
    CHECK_EQ((int)out[0].level, (int)DIAG_LEVEL_ERROR);
    CHECK_EQ((int)out[0].error_code, 0xBEEF);
    CHECK(std::string(out[0].message) == "boom");

    // NULL メッセージは空文字、NULL レコーダは無視
    diag_recorder_log(&r, DIAG_LEVEL_INFO, 1, nullptr, 5);
    CHECK_EQ(diag_recorder_get_logs(&r, out, 2), 2u);
    CHECK(std::string(out[1].message).empty());
    diag_recorder_log(nullptr, DIAG_LEVEL_INFO, 1, "x", 5);
}

TEST_CASE(recorder_message_truncation) {
    diag_recorder_t r;
    diag_recorder_init(&r);
    const std::string exact(DIAG_LOG_MSG_MAX_LEN - 1, 'a');      // 47 文字: そのまま
    const std::string longer(DIAG_LOG_MSG_MAX_LEN + 10, 'b');    // 58 文字: 47 文字に切り詰め
    diag_recorder_log(&r, DIAG_LEVEL_INFO, 1, exact.c_str(), 1);
    diag_recorder_log(&r, DIAG_LEVEL_INFO, 2, longer.c_str(), 2);
    diag_recorder_log(&r, DIAG_LEVEL_INFO, 3, "short", 3);
    diag_log_entry_t out[3];
    CHECK_EQ(diag_recorder_get_logs(&r, out, 3), 3u);
    CHECK(std::string(out[0].message) == exact);
    CHECK(std::string(out[1].message) == std::string(DIAG_LOG_MSG_MAX_LEN - 1, 'b'));
    CHECK_EQ((int)out[1].message[DIAG_LOG_MSG_MAX_LEN - 1], 0); // 終端
    CHECK(std::string(out[2].message) == "short");
}

TEST_CASE(recorder_message_is_always_terminated) {
    // スロットに終端のない古い内容が残っていても、切り詰めた新メッセージは必ず終端される
    diag_recorder_t r;
    diag_recorder_init(&r);
    r.entries[0].message[DIAG_LOG_MSG_MAX_LEN - 1] = 'Q';
    const std::string longer(DIAG_LOG_MSG_MAX_LEN + 10, 'c');
    diag_recorder_log(&r, DIAG_LEVEL_INFO, 1, longer.c_str(), 1);
    CHECK_EQ((int)r.entries[0].message[DIAG_LOG_MSG_MAX_LEN - 1], 0);
    CHECK_EQ(std::strlen(r.entries[0].message), (size_t)(DIAG_LOG_MSG_MAX_LEN - 1));
}

TEST_CASE(recorder_get_logs_does_not_write_past_count) {
    diag_recorder_t r;
    diag_recorder_init(&r);
    log_n(&r, 1, 3);
    diag_log_entry_t out[5];
    std::memset(out, 0x7E, sizeof(out));
    CHECK_EQ(diag_recorder_get_logs(&r, out, 3), 3u);
    for (size_t i = 3; i < 5; ++i) { // 戻り値の件数より先は触らない
        CHECK_EQ(out[i].timestamp_ms, 0x7E7E7E7Eu);
    }
    std::memset(out, 0x7E, sizeof(out));
    CHECK_EQ(diag_recorder_get_logs(&r, out, 2), 2u);
    CHECK_EQ(out[2].timestamp_ms, 0x7E7E7E7Eu);
}

TEST_CASE(recorder_message_does_not_leak_previous_entry) {
    // リングを一周して上書きしても、短い新メッセージに古い長いメッセージの残りが付かない
    diag_recorder_t r;
    diag_recorder_init(&r);
    const std::string longer(DIAG_LOG_MSG_MAX_LEN - 1, 'z');
    for (int i = 0; i < DIAG_MAX_CRASH_LOGS; ++i) diag_recorder_log(&r, DIAG_LEVEL_INFO, 0, longer.c_str(), 0);
    diag_recorder_log(&r, DIAG_LEVEL_INFO, 0, "ab", 0); // 最古のスロットを上書き
    diag_log_entry_t out[DIAG_MAX_CRASH_LOGS];
    CHECK_EQ(diag_recorder_get_logs(&r, out, DIAG_MAX_CRASH_LOGS), (size_t)DIAG_MAX_CRASH_LOGS);
    CHECK(std::string(out[DIAG_MAX_CRASH_LOGS - 1].message) == "ab");
}

TEST_CASE(recorder_order_before_full) {
    diag_recorder_t r;
    diag_recorder_init(&r);
    log_n(&r, 1, 5);
    CHECK_EQ(r.count, 5u);
    diag_log_entry_t out[DIAG_MAX_CRASH_LOGS];
    CHECK_EQ(diag_recorder_get_logs(&r, out, DIAG_MAX_CRASH_LOGS), 5u);
    for (int i = 0; i < 5; ++i) {
        CHECK_EQ(out[i].timestamp_ms, (uint32_t)(1001 + i)); // 古い順
        CHECK_EQ((int)out[i].error_code, 101 + i);
        CHECK_EQ((int)out[i].level, (1 + i) % 4);
        CHECK(std::string(out[i].message) == "msg" + std::to_string(1 + i));
    }
}

TEST_CASE(recorder_max_count_returns_newest) {
    diag_recorder_t r;
    diag_recorder_init(&r);
    log_n(&r, 1, 5);
    diag_log_entry_t out[8];
    // 上限を件数より小さくすると、新しい方から数えた件数を古い順に返す
    CHECK_EQ(diag_recorder_get_logs(&r, out, 3), 3u);
    CHECK_EQ(out[0].timestamp_ms, 1003u);
    CHECK_EQ(out[1].timestamp_ms, 1004u);
    CHECK_EQ(out[2].timestamp_ms, 1005u);
    CHECK_EQ(diag_recorder_get_logs(&r, out, 5), 5u);
    CHECK_EQ(out[0].timestamp_ms, 1001u);
    CHECK_EQ(diag_recorder_get_logs(&r, out, 1), 1u);
    CHECK_EQ(out[0].timestamp_ms, 1005u);
    // 上限が件数を超えても件数まで
    CHECK_EQ(diag_recorder_get_logs(&r, out, 8), 5u);

    CHECK_EQ(diag_recorder_get_logs(&r, out, 0), 0u);
    CHECK_EQ(diag_recorder_get_logs(&r, nullptr, 3), 0u);
    CHECK_EQ(diag_recorder_get_logs(nullptr, out, 3), 0u);
}

TEST_CASE(recorder_exactly_full) {
    diag_recorder_t r;
    diag_recorder_init(&r);
    log_n(&r, 1, DIAG_MAX_CRASH_LOGS);
    CHECK_EQ(r.count, (size_t)DIAG_MAX_CRASH_LOGS);
    CHECK_EQ(r.head, 0u); // 一周して先頭に戻る
    diag_log_entry_t out[DIAG_MAX_CRASH_LOGS + 4];
    CHECK_EQ(diag_recorder_get_logs(&r, out, DIAG_MAX_CRASH_LOGS + 4), (size_t)DIAG_MAX_CRASH_LOGS);
    for (int i = 0; i < DIAG_MAX_CRASH_LOGS; ++i) CHECK_EQ(out[i].timestamp_ms, (uint32_t)(1001 + i));
    CHECK_EQ(diag_recorder_get_logs(&r, out, 4), 4u);
    CHECK_EQ(out[0].timestamp_ms, (uint32_t)(1001 + DIAG_MAX_CRASH_LOGS - 4));
    CHECK_EQ(out[3].timestamp_ms, (uint32_t)(1000 + DIAG_MAX_CRASH_LOGS));
}

TEST_CASE(recorder_overwrites_oldest) {
    diag_recorder_t r;
    diag_recorder_init(&r);
    const int total = DIAG_MAX_CRASH_LOGS + 5; // 5 件上書き
    log_n(&r, 1, total);
    CHECK_EQ(r.count, (size_t)DIAG_MAX_CRASH_LOGS); // 件数は上限で頭打ち
    CHECK_EQ(r.head, 5u);
    diag_log_entry_t out[DIAG_MAX_CRASH_LOGS];
    CHECK_EQ(diag_recorder_get_logs(&r, out, DIAG_MAX_CRASH_LOGS), (size_t)DIAG_MAX_CRASH_LOGS);
    // 残るのは 6..21。最古 (1..5) が消え、古い順に並ぶ
    for (int i = 0; i < DIAG_MAX_CRASH_LOGS; ++i) {
        CHECK_EQ(out[i].timestamp_ms, (uint32_t)(1006 + i));
        CHECK(std::string(out[i].message) == "msg" + std::to_string(6 + i));
    }
    // 件数を絞ると最新側
    diag_log_entry_t few[3];
    CHECK_EQ(diag_recorder_get_logs(&r, few, 3), 3u);
    CHECK_EQ(few[0].timestamp_ms, (uint32_t)(1000 + total - 2));
    CHECK_EQ(few[2].timestamp_ms, (uint32_t)(1000 + total));

    // 2 周以上しても同じ
    log_n(&r, total + 1, total + 2 * DIAG_MAX_CRASH_LOGS + 3);
    const int last = total + 2 * DIAG_MAX_CRASH_LOGS + 3;
    CHECK_EQ(diag_recorder_get_logs(&r, out, DIAG_MAX_CRASH_LOGS), (size_t)DIAG_MAX_CRASH_LOGS);
    for (int i = 0; i < DIAG_MAX_CRASH_LOGS; ++i) {
        CHECK_EQ(out[i].timestamp_ms, (uint32_t)(1000 + last - DIAG_MAX_CRASH_LOGS + 1 + i));
    }
}

// ---- フォールトコンテキスト ----
namespace {
diag_fault_context_t make_ctx(uint32_t base) {
    diag_fault_context_t c;
    std::memset(&c, 0, sizeof(c));
    c.r0 = base + 0;
    c.r1 = base + 1;
    c.r2 = base + 2;
    c.r3 = base + 3;
    c.r12 = base + 4;
    c.lr = base + 5;
    c.pc = base + 6;
    c.psr = base + 7;
    c.cfsr = base + 8;
    c.hfsr = base + 9;
    c.mmfar = base + 10;
    c.bfar = base + 11;
    return c;
}
} // namespace

TEST_CASE(fault_context_recorded_and_overwritten) {
    diag_recorder_t r;
    diag_recorder_init(&r);
    CHECK(!r.has_crashed);

    diag_fault_context_t a = make_ctx(0x1000);
    diag_recorder_record_fault(&r, &a);
    CHECK(r.has_crashed);
    CHECK_EQ(r.last_fault.r0, 0x1000u);
    CHECK_EQ(r.last_fault.r1, 0x1001u);
    CHECK_EQ(r.last_fault.r2, 0x1002u);
    CHECK_EQ(r.last_fault.r3, 0x1003u);
    CHECK_EQ(r.last_fault.r12, 0x1004u);
    CHECK_EQ(r.last_fault.lr, 0x1005u);
    CHECK_EQ(r.last_fault.pc, 0x1006u);
    CHECK_EQ(r.last_fault.psr, 0x1007u);
    CHECK_EQ(r.last_fault.cfsr, 0x1008u);
    CHECK_EQ(r.last_fault.hfsr, 0x1009u);
    CHECK_EQ(r.last_fault.mmfar, 0x100Au);
    CHECK_EQ(r.last_fault.bfar, 0x100Bu);
    CHECK_EQ(r.count, 0u); // ログには入らない

    // 複数回: 最後のものが残り、フラグは立ったまま
    diag_fault_context_t b = make_ctx(0x2000);
    diag_recorder_record_fault(&r, &b);
    CHECK(r.has_crashed);
    CHECK_EQ(r.last_fault.pc, 0x2006u);
    CHECK_EQ(r.last_fault.bfar, 0x200Bu);

    // ログの追加ではフラグも保存済みコンテキストも変わらない
    diag_recorder_log(&r, DIAG_LEVEL_FATAL, 1, "after", 1);
    CHECK(r.has_crashed);
    CHECK_EQ(r.last_fault.pc, 0x2006u);

    // NULL は無視 (フラグを立てない / 内容を壊さない)
    diag_recorder_record_fault(&r, nullptr);
    diag_recorder_record_fault(nullptr, &a);
    CHECK_EQ(r.last_fault.pc, 0x2006u);
    diag_recorder_t fresh;
    diag_recorder_init(&fresh);
    diag_recorder_record_fault(&fresh, nullptr);
    CHECK(!fresh.has_crashed);

    // 初期化でクリアされる
    diag_recorder_init(&r);
    CHECK(!r.has_crashed);
    CHECK_EQ(r.last_fault.pc, 0u);
}

EMBED_TEST_MAIN()
