#include "embed_sched/embed_sched.h"
#include "embed_test/embed_test.hpp"
static void increment(void *p) { ++*(unsigned *)p; }
static uint64_t clock_value = 0;
static uint64_t clock_fn(void *) { return clock_value; }
static void slow(void *) { clock_value += 10; }
TEST_CASE(wheel_periodic_cancel_and_round_robin) {
    embed_sched s;
    unsigned a = 0, b = 0;
    embed_sched_init(&s, 0, clock_fn, nullptr);
    CHECK(embed_sched_start(&s, 0, 2, 2, increment, &a));
    CHECK(embed_sched_start(&s, 1, 65, 0, increment, &b));
    CHECK(embed_sched_advance(&s, 5));
    CHECK_EQ(a, 2u);
    CHECK_EQ(b, 0u);
    CHECK_EQ(s.missed, 2u);
    embed_sched_cancel(&s, 0);
    CHECK(embed_sched_advance(&s, 65));
    CHECK_EQ(b, 1u);
    CHECK(!embed_sched_advance(&s, 64));
    CHECK(!embed_sched_start(&s, 0, 0, 0, increment, &a));
    CHECK(embed_sched_add_task(&s, increment, &a, 0));
    CHECK(embed_sched_add_task(&s, increment, &b, 0));
    clock_value = 65;
    CHECK(embed_sched_run_once(&s));
    CHECK(embed_sched_run_once(&s));
    CHECK_EQ(a, 3u);
    CHECK_EQ(b, 2u);
    embed_sched_init(&s, 65, clock_fn, nullptr);
    CHECK(embed_sched_add_task(&s, slow, nullptr, 5));
    CHECK(embed_sched_run_once(&s));
    CHECK_EQ(s.missed, 1u);
}

struct mutation {
    embed_sched *scheduler;
    unsigned calls;
};
static void cancel_self(void *user) {
    auto *m = (mutation *)user;
    ++m->calls;
    embed_sched_cancel(m->scheduler, 0);
    CHECK(!embed_sched_advance(m->scheduler, m->scheduler->now));
}
static void cancel_other(void *user) { embed_sched_cancel((embed_sched *)user, 1); }
TEST_CASE(callback_mutation_and_overflow) {
    embed_sched s;
    embed_sched_init(&s, 0, nullptr, nullptr);
    mutation m{&s, 0};
    CHECK(embed_sched_start(&s, 0, 1, 1, cancel_self, &m));
    CHECK(embed_sched_advance(&s, 10));
    CHECK_EQ(m.calls, 1u);
    unsigned calls = 0;
    CHECK(embed_sched_start(&s, 1, 1, 0, increment, &calls));
    CHECK(embed_sched_start(&s, 0, 1, 0, cancel_other, &s));
    CHECK(embed_sched_advance(&s, 11));
    CHECK_EQ(calls, 0u);
    embed_sched_init(&s, UINT64_MAX - 1, nullptr, nullptr);
    CHECK(!embed_sched_start(&s, 0, 2, 0, increment, &calls));
    CHECK(embed_sched_start(&s, 0, 1, 1, increment, &calls));
    CHECK(embed_sched_advance(&s, UINT64_MAX));
    CHECK_EQ(calls, 1u);
    CHECK(!s.timers[0].active);
}
EMBED_TEST_MAIN()
