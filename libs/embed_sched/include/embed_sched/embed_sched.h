#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define EMBED_SCHED_SLOTS 64
#define EMBED_SCHED_TIMERS 32
#define EMBED_SCHED_TASKS 16
typedef void (*embed_sched_fn)(void *);
typedef uint64_t (*embed_sched_clock)(void *);
typedef struct {
    uint64_t due, period;
    embed_sched_fn fn;
    void *user;
    int prev, next;
    bool active;
} embed_timer;
typedef struct {
    embed_sched_fn fn;
    void *user;
    uint64_t budget;
} embed_task;
typedef struct {
    uint64_t now, missed;
    int buckets[EMBED_SCHED_SLOTS];
    embed_timer timers[EMBED_SCHED_TIMERS];
    embed_task tasks[EMBED_SCHED_TASKS];
    unsigned task_count, cursor;
    embed_sched_clock clock;
    void *clock_user;
    bool running;
} embed_sched;
void embed_sched_init(embed_sched *, uint64_t now, embed_sched_clock, void *);
/* Explicit timer IDs: O(1) start/cancel. delay must be >=1; period==0 is one-shot. */
bool embed_sched_start(embed_sched *, unsigned id, uint64_t delay, uint64_t period, embed_sched_fn,
                       void *);
void embed_sched_cancel(embed_sched *, unsigned id);
bool embed_sched_advance(embed_sched *, uint64_t now);
bool embed_sched_add_task(embed_sched *, embed_sched_fn, void *, uint64_t budget);
/* Poll monotonic clock, expire timers and run one round-robin task. */
bool embed_sched_run_once(embed_sched *);

#ifdef __cplusplus
}
#endif
