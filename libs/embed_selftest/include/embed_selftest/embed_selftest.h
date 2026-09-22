#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
#include "embed_crc/embed_crc.h"
/* Destructive: exclusively owned scratch RAM only; leaves zero on success. */
bool embed_selftest_march_c(volatile uint32_t *ram,size_t words);
bool embed_selftest_rom(const void *rom,size_t len,uint32_t expected_crc32);
void embed_selftest_stack_fill(void *unused_stack,size_t len,uint8_t marker);
size_t embed_selftest_stack_unused(const void *stack,size_t len,uint8_t marker,bool grows_down);
/* CPU register test is platform assembly supplied by the application. NULL fails closed. */
typedef bool (*embed_selftest_cpu_fn)(void *);
bool embed_selftest_cpu(embed_selftest_cpu_fn,void *);
bool embed_selftest_clock(uint64_t measured,uint64_t expected,uint32_t tolerance_ppm);
typedef struct { uint64_t last,min_interval,max_interval;void (*feed)(void *);void *user;bool fault; } embed_watchdog;
bool embed_watchdog_init(embed_watchdog *,uint64_t now,uint64_t min_interval,uint64_t max_interval,void (*feed)(void *),void *);
/* Feed only when all checks are healthy and inside the configured window. Faults latch. */
bool embed_watchdog_poll(embed_watchdog *,uint64_t now,bool healthy);

#ifdef __cplusplus
}
#endif
