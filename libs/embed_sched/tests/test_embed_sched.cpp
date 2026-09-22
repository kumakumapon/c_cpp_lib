#include "embed_sched/embed_sched.h"
#include "embed_test/embed_test.hpp"
static void increment(void *p) {++*(unsigned*)p;}
static uint64_t clock_value=0;
static uint64_t clock_fn(void*) {return clock_value;}
static void slow(void*) {clock_value+=10;}
TEST_CASE(wheel_periodic_cancel_and_round_robin) {
    embed_sched s;unsigned a=0,b=0;embed_sched_init(&s,0,clock_fn,nullptr);
    CHECK(embed_sched_start(&s,0,2,2,increment,&a));
    CHECK(embed_sched_start(&s,1,65,0,increment,&b));
    CHECK(embed_sched_advance(&s,5));CHECK_EQ(a,2u);CHECK_EQ(b,0u);CHECK_EQ(s.missed,2u);
    embed_sched_cancel(&s,0);CHECK(embed_sched_advance(&s,65));CHECK_EQ(b,1u);
    CHECK(!embed_sched_advance(&s,64));CHECK(!embed_sched_start(&s,0,0,0,increment,&a));
    CHECK(embed_sched_add_task(&s,increment,&a,0));CHECK(embed_sched_add_task(&s,increment,&b,0));
    clock_value=65;CHECK(embed_sched_run_once(&s));CHECK(embed_sched_run_once(&s));CHECK_EQ(a,3u);CHECK_EQ(b,2u);
    embed_sched_init(&s,65,clock_fn,nullptr);CHECK(embed_sched_add_task(&s,slow,nullptr,5));CHECK(embed_sched_run_once(&s));CHECK_EQ(s.missed,1u);
}

EMBED_TEST_MAIN()
