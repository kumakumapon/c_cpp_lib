#pragma once
#ifndef DIAG_TYPES_H
#define DIAG_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DIAG_MAX_CRASH_LOGS        16
#define DIAG_LOG_MSG_MAX_LEN       48
#define DIAG_CANARY_PATTERN        0xA5A5A5A5U

/**
 * @brief 障害ログレベル
 */
typedef enum {
    DIAG_LEVEL_INFO    = 0,
    DIAG_LEVEL_WARN    = 1,
    DIAG_LEVEL_ERROR   = 2,
    DIAG_LEVEL_FATAL   = 3
} diag_log_level_t;

/**
 * @brief 巡回ログエントリ構造体
 */
typedef struct {
    uint32_t timestamp_ms;
    diag_log_level_t level;
    uint16_t error_code;
    char message[DIAG_LOG_MSG_MAX_LEN];
} diag_log_entry_t;

/**
 * @brief ハードフォールト・レジスタダンプ構造体 (ARM Cortex-M / 汎用マイコン互換)
 */
typedef struct {
    uint32_t r0;
    uint32_t r1;
    uint32_t r2;
    uint32_t r3;
    uint32_t r12;
    uint32_t lr;
    uint32_t pc;
    uint32_t psr;
    uint32_t cfsr;  /**< Configurable Fault Status Register */
    uint32_t hfsr;  /**< HardFault Status Register */
    uint32_t mmfar; /**< MemManage Fault Address Register */
    uint32_t bfar;  /**< BusFault Address Register */
} diag_fault_context_t;

/**
 * @brief スタック監視構造体
 */
typedef struct {
    uint8_t *stack_start;
    size_t stack_size;
    uint32_t canary_depth;
    size_t high_water_mark; /**< 最大スタック使用バイト数 */
} diag_stack_monitor_t;

/**
 * @brief CPU使用率計測構造体
 */
typedef struct {
    uint64_t total_time_us;
    uint64_t active_time_us;
    uint64_t last_calc_time_us;
    float current_cpu_usage_pct;
} diag_cpu_monitor_t;

/**
 * @brief ブラックボックス障害レコーダー構造体
 */
typedef struct {
    diag_log_entry_t entries[DIAG_MAX_CRASH_LOGS];
    size_t head;
    size_t count;
    bool has_crashed;
    diag_fault_context_t last_fault;
} diag_recorder_t;

#ifdef __cplusplus
}
#endif

#endif /* DIAG_TYPES_H */
