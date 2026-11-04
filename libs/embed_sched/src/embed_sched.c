#include "embed_sched/embed_sched.h"
#include <string.h>
void embed_sched_init(embed_sched *s, uint64_t now, embed_sched_clock clock, void *user) {
    if (!s)
        return;
    memset(s, 0, sizeof(*s));
    s->now = now;
    s->clock = clock;
    s->clock_user = user;
    for (unsigned i = 0; i < EMBED_SCHED_SLOTS; ++i)
        s->buckets[i] = -1;
}
void embed_sched_cancel(embed_sched *s, unsigned id) {
    if (!s || id >= EMBED_SCHED_TIMERS || !s->timers[id].active)
        return;
    embed_timer *t = &s->timers[id];
    if (t->prev >= 0)
        s->timers[t->prev].next = t->next;
    else
        s->buckets[t->due % EMBED_SCHED_SLOTS] = t->next;
    if (t->next >= 0)
        s->timers[t->next].prev = t->prev;
    t->active = false;
}
bool embed_sched_start(embed_sched *s, unsigned id, uint64_t delay, uint64_t period,
                       embed_sched_fn fn, void *user) {
    if (!s || id >= EMBED_SCHED_TIMERS || !delay || !fn || delay > UINT64_MAX - s->now)
        return false;
    embed_sched_cancel(s, id);
    embed_timer *t = &s->timers[id];
    t->due = s->now + delay;
    t->period = period;
    t->fn = fn;
    t->user = user;
    t->active = true;
    t->prev = -1;
    unsigned bucket = (unsigned)(t->due % EMBED_SCHED_SLOTS);
    t->next = s->buckets[bucket];
    if (t->next >= 0)
        s->timers[t->next].prev = (int)id;
    s->buckets[bucket] = (int)id;
    return true;
}
bool embed_sched_is_active(const embed_sched *s, unsigned id) {
    return s && id < EMBED_SCHED_TIMERS && s->timers[id].active;
}
bool embed_sched_remaining(const embed_sched *s, unsigned id, uint64_t *ticks) {
    if (!embed_sched_is_active(s, id) || !ticks)
        return false;
    *ticks = s->timers[id].due - s->now;
    return true;
}
bool embed_sched_next_due(const embed_sched *s, uint64_t *due) {
    if (!s || !due)
        return false;
    bool found = false;
    uint64_t best = UINT64_MAX;
    for (unsigned i = 0; i < EMBED_SCHED_TIMERS; ++i)
        if (s->timers[i].active && s->timers[i].due <= best) {
            best = s->timers[i].due;
            found = true;
        }
    if (found)
        *due = best;
    return found;
}
static void expire_current_tick(embed_sched *s, uint64_t target) {
    unsigned ready[EMBED_SCHED_TIMERS], n = 0;
    for (int i = s->buckets[s->now % EMBED_SCHED_SLOTS]; i >= 0; i = s->timers[i].next)
        if (s->timers[i].due == s->now)
            ready[n++] = (unsigned)i;
    for (unsigned j = 0; j < n; ++j) {
        unsigned id = ready[j];
        embed_timer t = s->timers[id];
        /* 先に実行したコールバックが取り消し・再設定していたら実行しない */
        if (!t.active || t.due != s->now)
            continue;
        embed_sched_cancel(s, id);
        if (target > t.due) {
            ++s->late_timers;
            ++s->missed;
        }
        if (t.period)
            embed_sched_start(s, id, t.period, t.period, t.fn, t.user);
        t.fn(t.user);
    }
}
bool embed_sched_advance(embed_sched *s, uint64_t now) {
    if (!s || now < s->now || s->running)
        return false;
    s->running = true;
    while (s->now < now) {
        /* 大きな時間ジャンプでは、次の期限の直前まで一気に進める (tick ごとの空回りを避ける) */
        if (now - s->now > EMBED_SCHED_SLOTS) {
            uint64_t due;
            if (!embed_sched_next_due(s, &due) || due > now) {
                s->now = now;
                break;
            }
            if (due > s->now + 1)
                s->now = due - 1;
        }
        ++s->now;
        expire_current_tick(s, now);
    }
    s->running = false;
    return true;
}
bool embed_sched_add_task(embed_sched *s, embed_sched_fn fn, void *user, uint64_t budget) {
    if (!s || !fn || s->task_count >= EMBED_SCHED_TASKS)
        return false;
    s->tasks[s->task_count++] = (embed_task){fn, user, budget};
    return true;
}
bool embed_sched_remove_task(embed_sched *s, embed_sched_fn fn, void *user) {
    /* タスク・タイマのコールバック内から呼んでもよい (run_once は実行中タスクのコピーを使う) */
    if (!s)
        return false;
    for (unsigned i = 0; i < s->task_count; ++i) {
        if (s->tasks[i].fn != fn || s->tasks[i].user != user)
            continue;
        memmove(&s->tasks[i], &s->tasks[i + 1], (s->task_count - i - 1) * sizeof(embed_task));
        --s->task_count;
        /* 外したタスクより後ろを指していたカーソルは 1 つ詰める */
        if (s->cursor > i)
            --s->cursor;
        if (s->cursor >= s->task_count)
            s->cursor = 0;
        return true;
    }
    return false;
}
bool embed_sched_run_once(embed_sched *s) {
    if (!s || !s->clock || !embed_sched_advance(s, s->clock(s->clock_user)))
        return false;
    if (s->task_count) {
        embed_task t = s->tasks[s->cursor];
        s->cursor = (s->cursor + 1) % s->task_count;
        s->running = true;
        uint64_t start = s->clock(s->clock_user);
        t.fn(t.user);
        uint64_t end = s->clock(s->clock_user);
        s->running = false;
        if (end < start)
            return false;
        if (t.budget && end - start > t.budget) {
            ++s->task_overruns;
            ++s->missed;
        }
    }
    return true;
}
