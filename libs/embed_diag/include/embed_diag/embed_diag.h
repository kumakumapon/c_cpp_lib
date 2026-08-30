#pragma once
#ifndef EMBED_DIAG_H
#define EMBED_DIAG_H

#include "diag_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------
 * スタック監視 API
 * --------------------------------------------------------- */

/**
 * @brief スタック領域を初期化し、カナリアパターン (0xA5) を充填します。
 */
void diag_stack_init(diag_stack_monitor_t *mon, void *stack_buffer, size_t stack_size);

/**
 * @brief スタックの空き容量および最大使用量を測定します。
 * @param mon 監視構造体
 * @param free_bytes 空きバイト数出力先 (NULL可)
 * @param used_bytes 最大使用バイト数出力先 (NULL可)
 * @return true カナリアが正常（オーバーフロー未発生）、false オーバーフロー検知
 */
bool diag_stack_check(diag_stack_monitor_t *mon, size_t *free_bytes, size_t *used_bytes);

/* ---------------------------------------------------------
 * CPU使用率モニタ API
 * --------------------------------------------------------- */

void diag_cpu_init(diag_cpu_monitor_t *mon);
void diag_cpu_update(diag_cpu_monitor_t *mon, uint64_t active_time_us, uint64_t total_time_us);
float diag_cpu_get_usage(const diag_cpu_monitor_t *mon);

/* ---------------------------------------------------------
 * ブラックボックス・障害ログレコーダー API
 * --------------------------------------------------------- */

void diag_recorder_init(diag_recorder_t *recorder);
void diag_recorder_log(diag_recorder_t *recorder, diag_log_level_t level, uint16_t error_code, const char *msg, uint32_t timestamp_ms);
void diag_recorder_record_fault(diag_recorder_t *recorder, const diag_fault_context_t *ctx);
size_t diag_recorder_get_logs(const diag_recorder_t *recorder, diag_log_entry_t *dest, size_t max_count);

#ifdef __cplusplus
}
#endif

#endif /* EMBED_DIAG_H */
