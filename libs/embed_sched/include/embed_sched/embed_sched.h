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
    uint64_t now;
    uint64_t missed;        /* late_timers + task_overruns (互換のための合計) */
    uint64_t late_timers;   /* advance の目標時刻より前が期限だった (遅れて実行した) タイマ */
    uint64_t task_overruns; /* budget を超えたタスク実行 */
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
bool embed_sched_is_active(const embed_sched *, unsigned id);
/* 期限までの残り tick (期限ちょうどなら 0)。未起動なら false。 */
bool embed_sched_remaining(const embed_sched *, unsigned id, uint64_t *ticks);
/* 最も早い期限 (絶対時刻)。起動中のタイマがなければ false。tickless スリープの起床時刻に使う。 */
bool embed_sched_next_due(const embed_sched *, uint64_t *due);
bool embed_sched_advance(embed_sched *, uint64_t now);
bool embed_sched_add_task(embed_sched *, embed_sched_fn, void *, uint64_t budget);
/* fn と user が一致する最初のタスクを外す。ラウンドロビンの順序は保つ。 */
bool embed_sched_remove_task(embed_sched *, embed_sched_fn, void *);
/* Poll monotonic clock, expire timers and run one round-robin task. */
bool embed_sched_run_once(embed_sched *);

#ifdef __cplusplus
}
#endif
