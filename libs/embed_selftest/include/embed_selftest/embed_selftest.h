#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#include "embed_crc/embed_crc.h"
/* Destructive: exclusively owned scratch RAM only; leaves zero on success. */
bool embed_selftest_march_c(volatile uint32_t *ram, size_t words);
bool embed_selftest_rom(const void *rom, size_t len, uint32_t expected_crc32);

/* ---- 抽象メモリ (外付け SRAM/FRAM をドライバ経由で検査する、または故障モデルで検証する) ---- */
typedef struct {
    uint32_t (*read)(void *ctx, size_t index);
    void (*write)(void *ctx, size_t index, uint32_t value);
    void *ctx;
} embed_selftest_mem_io;
/* 内蔵 RAM を抽象メモリとして扱うアダプタ。ram は検査の間有効であること。 */
void embed_selftest_ram_io(embed_selftest_mem_io *io, volatile uint32_t *ram);
/* March C- (破壊的)。失敗時、fail_index (NULL 可) に最初に不一致を検出した語の位置を入れる。
 * 固着・遷移・アドレスデコーダ・語間結合故障を検出する。 */
bool embed_selftest_march_c_io(const embed_selftest_mem_io *io, size_t words, size_t *fail_index);
/* データバス: 1 語に walking-one を書いて読み戻す (破壊的)。データ線の固着・短絡を検出。
 * 失敗時、failed_pattern (NULL 可) に読み戻せなかった書き込み値を入れる。 */
bool embed_selftest_data_bus(const embed_selftest_mem_io *io, size_t index, uint32_t *failed_pattern);
/* アドレスバス: 2 の累乗オフセットで検査 (破壊的、words >= 2)。アドレス線の固着・短絡を検出。
 * 失敗時、fail_index (NULL 可) に問題を検出したオフセットを入れる。 */
bool embed_selftest_address_bus(const embed_selftest_mem_io *io, size_t words, size_t *fail_index);

/* ---- ROM の周期検査 (CPU 時間を分割して少しずつ CRC を計算する) ---- */
typedef enum {
    EMBED_SELFTEST_BUSY = 0, /* 計算途中 */
    EMBED_SELFTEST_PASS = 1, /* 1 周終わり、期待値と一致 (次の step から先頭に戻る) */
    EMBED_SELFTEST_FAIL = -1 /* 1 周終わり、不一致 (次の step から先頭に戻る) */
} embed_selftest_status;
typedef struct {
    const uint8_t *rom;
    size_t len, pos;
    uint32_t state, expected;
} embed_selftest_rom_ctx;
bool embed_selftest_rom_begin(embed_selftest_rom_ctx *ctx, const void *rom, size_t len,
                              uint32_t expected_crc32);
/* 最大 chunk バイトを処理する (chunk >= 1)。引数不正は FAIL。 */
embed_selftest_status embed_selftest_rom_step(embed_selftest_rom_ctx *ctx, size_t chunk);
void embed_selftest_stack_fill(void *unused_stack, size_t len, uint8_t marker);
size_t embed_selftest_stack_unused(const void *stack, size_t len, uint8_t marker, bool grows_down);
/* CPU register test is platform assembly supplied by the application. NULL fails closed. */
typedef bool (*embed_selftest_cpu_fn)(void *);
bool embed_selftest_cpu(embed_selftest_cpu_fn, void *);
bool embed_selftest_clock(uint64_t measured, uint64_t expected, uint32_t tolerance_ppm);
typedef struct {
    uint64_t last, min_interval, max_interval;
    void (*feed)(void *);
    void *user;
    bool fault;
} embed_watchdog;
bool embed_watchdog_init(embed_watchdog *, uint64_t now, uint64_t min_interval,
                         uint64_t max_interval, void (*feed)(void *), void *);
/* Feed only when all checks are healthy and inside the configured window. Faults latch. */
bool embed_watchdog_poll(embed_watchdog *, uint64_t now, bool healthy);

#ifdef __cplusplus
}
#endif
