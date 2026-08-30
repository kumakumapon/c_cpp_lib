#pragma once
#ifndef EMBED_LOG_H
#define EMBED_LOG_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------
 * ログレベル定義
 * --------------------------------------------------------- */
typedef enum {
    EMBED_LOG_LEVEL_NONE  = 0,
    EMBED_LOG_LEVEL_FATAL = 1,
    EMBED_LOG_LEVEL_ERROR = 2,
    EMBED_LOG_LEVEL_WARN  = 3,
    EMBED_LOG_LEVEL_INFO  = 4,
    EMBED_LOG_LEVEL_DEBUG = 5,
    EMBED_LOG_LEVEL_TRACE = 6,
} embed_log_level_t;

/* コンパイル時最大ログレベル (これより詳細なログはバイナリから完全消去) */
#ifndef EMBED_LOG_MAX_COMPILE_LEVEL
#define EMBED_LOG_MAX_COMPILE_LEVEL EMBED_LOG_LEVEL_TRACE
#endif

/* 1行の最大ログ長 (スタックバッファサイズ) */
#ifndef EMBED_LOG_MAX_LINE_LENGTH
#define EMBED_LOG_MAX_LINE_LENGTH 256
#endif

/* 最大バックエンド出力数 (UART, RTT, Flash, etc.) */
#ifndef EMBED_LOG_MAX_BACKENDS
#define EMBED_LOG_MAX_BACKENDS 4
#endif

/* ---------------------------------------------------------
 * コールバック型定義
 * --------------------------------------------------------- */

/**
 * @brief バックエンド出力関数型 (UART送信やRTT出力用)
 * @param str 出力文字列 (ヌル終端)
 * @param length 文字列長
 * @param user_data 登録時ユーザーポインタ
 */
typedef void (*embed_log_output_fn)(const char *str, size_t length, void *user_data);

/**
 * @brief タイムスタンプ取得関数型 (msまたはtickを返す)
 */
typedef uint32_t (*embed_log_get_time_fn)(void);

/**
 * @brief 排他制御関数型
 */
typedef void (*embed_log_lock_fn)(void *user_data);
typedef void (*embed_log_unlock_fn)(void *user_data);

/* ---------------------------------------------------------
 * 初期化 & 設定 API
 * --------------------------------------------------------- */

/**
 * @brief ロガーを初期化します。
 */
void embed_log_init(void);

/**
 * @brief 出力バックエンドを追加登録します。
 * @return true 登録成功, false 最大数超過
 */
bool embed_log_add_backend(embed_log_output_fn output_fn, void *user_data);

/**
 * @brief 登録済みバックエンドを全クリアします。
 */
void embed_log_clear_backends(void);

/**
 * @brief 実行時ログレベルを設定します。
 */
void embed_log_set_level(embed_log_level_t level);

/**
 * @brief 現在の実行時ログレベルを取得します。
 */
embed_log_level_t embed_log_get_level(void);

/**
 * @brief タイムスタンプ関数を設定します。
 */
void embed_log_set_timestamp_fn(embed_log_get_time_fn time_fn);

/**
 * @brief ANSIカラー出力の有効/無効を切り替えます。
 */
void embed_log_set_color_enabled(bool enabled);

/**
 * @brief 排他制御用コールバックを設定します。
 */
void embed_log_set_lock(embed_log_lock_fn lock, embed_log_unlock_fn unlock, void *user_data);

/* ---------------------------------------------------------
 * 出力コア API
 * --------------------------------------------------------- */

/**
 * @brief フォーマット付きログを出力します (内部関数)。
 */
void embed_log_write(embed_log_level_t level, const char *tag, const char *file, int line,
                     const char *fmt, ...);

void embed_log_write_va(embed_log_level_t level, const char *tag, const char *file, int line,
                        const char *fmt, va_list args);

/**
 * @brief メモリダンプ (Hex Dump) を出力します。
 * @param level ログレベル
 * @param tag モジュールタグ
 * @param data ダンプ対象データ
 * @param size データサイズ
 */
void embed_log_hexdump(embed_log_level_t level, const char *tag, const void *data, size_t size);

/* ---------------------------------------------------------
 * 簡易ロギングマクロ
 * --------------------------------------------------------- */
#if (EMBED_LOG_MAX_COMPILE_LEVEL >= EMBED_LOG_LEVEL_FATAL)
#define EMBED_LOG_FATAL(tag, fmt, ...) embed_log_write(EMBED_LOG_LEVEL_FATAL, tag, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#else
#define EMBED_LOG_FATAL(tag, fmt, ...) ((void)0)
#endif

#if (EMBED_LOG_MAX_COMPILE_LEVEL >= EMBED_LOG_LEVEL_ERROR)
#define EMBED_LOG_ERROR(tag, fmt, ...) embed_log_write(EMBED_LOG_LEVEL_ERROR, tag, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#else
#define EMBED_LOG_ERROR(tag, fmt, ...) ((void)0)
#endif

#if (EMBED_LOG_MAX_COMPILE_LEVEL >= EMBED_LOG_LEVEL_WARN)
#define EMBED_LOG_WARN(tag, fmt, ...) embed_log_write(EMBED_LOG_LEVEL_WARN, tag, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#else
#define EMBED_LOG_WARN(tag, fmt, ...) ((void)0)
#endif

#if (EMBED_LOG_MAX_COMPILE_LEVEL >= EMBED_LOG_LEVEL_INFO)
#define EMBED_LOG_INFO(tag, fmt, ...) embed_log_write(EMBED_LOG_LEVEL_INFO, tag, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#else
#define EMBED_LOG_INFO(tag, fmt, ...) ((void)0)
#endif

#if (EMBED_LOG_MAX_COMPILE_LEVEL >= EMBED_LOG_LEVEL_DEBUG)
#define EMBED_LOG_DEBUG(tag, fmt, ...) embed_log_write(EMBED_LOG_LEVEL_DEBUG, tag, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#else
#define EMBED_LOG_DEBUG(tag, fmt, ...) ((void)0)
#endif

#if (EMBED_LOG_MAX_COMPILE_LEVEL >= EMBED_LOG_LEVEL_TRACE)
#define EMBED_LOG_TRACE(tag, fmt, ...) embed_log_write(EMBED_LOG_LEVEL_TRACE, tag, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#else
#define EMBED_LOG_TRACE(tag, fmt, ...) ((void)0)
#endif

/* 短縮形マクロ */
#define LOG_F(tag, fmt, ...) EMBED_LOG_FATAL(tag, fmt, ##__VA_ARGS__)
#define LOG_E(tag, fmt, ...) EMBED_LOG_ERROR(tag, fmt, ##__VA_ARGS__)
#define LOG_W(tag, fmt, ...) EMBED_LOG_WARN(tag, fmt, ##__VA_ARGS__)
#define LOG_I(tag, fmt, ...) EMBED_LOG_INFO(tag, fmt, ##__VA_ARGS__)
#define LOG_D(tag, fmt, ...) EMBED_LOG_DEBUG(tag, fmt, ##__VA_ARGS__)
#define LOG_T(tag, fmt, ...) EMBED_LOG_TRACE(tag, fmt, ##__VA_ARGS__)
#define LOG_HEXDUMP(tag, data, size) embed_log_hexdump(EMBED_LOG_LEVEL_DEBUG, tag, data, size)

#ifdef __cplusplus
}
#endif

#endif /* EMBED_LOG_H */
