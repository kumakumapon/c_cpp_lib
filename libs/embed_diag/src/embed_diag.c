#include "embed_diag/embed_diag.h"
#include <string.h>

/* ---------------------------------------------------------
 * スタック監視実装
 * --------------------------------------------------------- */

void diag_stack_init(diag_stack_monitor_t *mon, void *stack_buffer, size_t stack_size) {
    if (!mon || !stack_buffer || stack_size < 16) return;

    mon->stack_start = (uint8_t *)stack_buffer;
    mon->stack_size = stack_size;
    mon->canary_depth = 16;
    mon->high_water_mark = 0;

    // スタック領域全体を 0xA5 パターンで満たす
    memset(mon->stack_start, 0xA5, stack_size);
}

bool diag_stack_check(diag_stack_monitor_t *mon, size_t *free_bytes, size_t *used_bytes) {
    if (!mon || !mon->stack_start) return false;

    // 先頭（スタックの底）から 0xA5 でないバイトが現れる位置を走査
    size_t unused = 0;
    while (unused < mon->stack_size && mon->stack_start[unused] == 0xA5) {
        unused++;
    }

    size_t used = mon->stack_size - unused;
    if (used > mon->high_water_mark) {
        mon->high_water_mark = used;
    }

    if (free_bytes) *free_bytes = unused;
    if (used_bytes) *used_bytes = mon->high_water_mark;

    // 底部のカナリア領域が破壊されていたらオーバーフロー
    return (unused >= mon->canary_depth);
}

/* ---------------------------------------------------------
 * CPU使用率モニタ実装
 * --------------------------------------------------------- */

void diag_cpu_init(diag_cpu_monitor_t *mon) {
    if (!mon) return;
    memset(mon, 0, sizeof(diag_cpu_monitor_t));
}

void diag_cpu_update(diag_cpu_monitor_t *mon, uint64_t active_time_us, uint64_t total_time_us) {
    if (!mon || total_time_us == 0) return;

    mon->active_time_us += active_time_us;
    mon->total_time_us += total_time_us;

    if (total_time_us > 0) {
        float pct = ((float)active_time_us / (float)total_time_us) * 100.0f;
        if (pct > 100.0f) pct = 100.0f;
        if (pct < 0.0f) pct = 0.0f;
        mon->current_cpu_usage_pct = pct;
    }
}

float diag_cpu_get_usage(const diag_cpu_monitor_t *mon) {
    return mon ? mon->current_cpu_usage_pct : 0.0f;
}

/* ---------------------------------------------------------
 * 障害ログレコーダー実装
 * --------------------------------------------------------- */

void diag_recorder_init(diag_recorder_t *recorder) {
    if (!recorder) return;
    memset(recorder, 0, sizeof(diag_recorder_t));
}

void diag_recorder_log(diag_recorder_t *recorder, diag_log_level_t level, uint16_t error_code, const char *msg, uint32_t timestamp_ms) {
    if (!recorder) return;

    diag_log_entry_t *entry = &recorder->entries[recorder->head];
    entry->timestamp_ms = timestamp_ms;
    entry->level = level;
    entry->error_code = error_code;

    if (msg) {
        strncpy(entry->message, msg, sizeof(entry->message) - 1);
        entry->message[sizeof(entry->message) - 1] = '\0';
    } else {
        entry->message[0] = '\0';
    }

    recorder->head = (recorder->head + 1) % DIAG_MAX_CRASH_LOGS;
    if (recorder->count < DIAG_MAX_CRASH_LOGS) {
        recorder->count++;
    }
}

void diag_recorder_record_fault(diag_recorder_t *recorder, const diag_fault_context_t *ctx) {
    if (!recorder || !ctx) return;
    recorder->has_crashed = true;
    recorder->last_fault = *ctx;
}

size_t diag_recorder_get_logs(const diag_recorder_t *recorder, diag_log_entry_t *dest, size_t max_count) {
    if (!recorder || !dest || max_count == 0) return 0;

    size_t n = (recorder->count < max_count) ? recorder->count : max_count;
    size_t start_idx = 0;
    if (recorder->count == DIAG_MAX_CRASH_LOGS) {
        start_idx = (recorder->head + DIAG_MAX_CRASH_LOGS - n) % DIAG_MAX_CRASH_LOGS;
    } else {
        start_idx = (recorder->count >= n) ? (recorder->count - n) : 0;
    }

    for (size_t i = 0; i < n; ++i) {
        size_t idx = (start_idx + i) % DIAG_MAX_CRASH_LOGS;
        dest[i] = recorder->entries[idx];
    }

    return n;
}
