// タイマホイールの発火時刻・周期のずれなし・バケット衝突・大きな時間ジャンプ・
// 問い合わせ API・ラウンドロビンとタスク削除・予算超過の境界を固定する。
#include "embed_sched/embed_sched.h"
#include "embed_test/embed_test.hpp"
#include <vector>

namespace {
struct Log {
    embed_sched *s;
    std::vector<uint64_t> at; // コールバック時の s->now
};
void record(void *u) {
    auto *l = static_cast<Log *>(u);
    l->at.push_back(l->s->now);
}
void step_to(embed_sched &s, uint64_t until) {
    while (s.now < until)
        REQUIRE(embed_sched_advance(&s, s.now + 1));
}
uint64_t fake_clock = 0;
uint64_t clock_fn(void *) { return fake_clock; }
std::vector<int> order;
uint64_t task_cost = 0;
void task_a(void *) { order.push_back(0); fake_clock += task_cost; }
void task_b(void *) { order.push_back(1); fake_clock += task_cost; }
void task_c(void *) { order.push_back(2); fake_clock += task_cost; }
void task_back(void *) { fake_clock -= 1; }
} // namespace

TEST_CASE(one_shot_fires_once_at_due) {
    embed_sched s;
    embed_sched_init(&s, 10, nullptr, nullptr);
    Log log{&s, {}};
    REQUIRE(embed_sched_start(&s, 3, 5, 0, record, &log));
    step_to(s, 14);
    CHECK(log.at.empty());
    step_to(s, 30);
    REQUIRE_EQ(log.at.size(), 1u);
    CHECK_EQ(log.at[0], 15u);
    CHECK(!embed_sched_is_active(&s, 3));
    CHECK_EQ(s.late_timers, 0u); // 1 tick ずつ進めれば遅れなし
}

TEST_CASE(periodic_has_no_drift_even_when_polled_late) {
    for (int mode = 0; mode < 2; ++mode) {
        embed_sched s;
        embed_sched_init(&s, 0, nullptr, nullptr);
        Log log{&s, {}};
        REQUIRE(embed_sched_start(&s, 0, 3, 7, record, &log));
        if (mode == 0)
            step_to(s, 100);
        else
            REQUIRE(embed_sched_advance(&s, 100)); // まとめて進めても期限は 3, 10, 17, ...
        REQUIRE_EQ(log.at.size(), 14u);              // 3 + 7k <= 100 (k = 0..13)
        for (size_t k = 0; k < log.at.size(); ++k)
            CHECK_EQ(log.at[k], 3u + 7u * k);
        CHECK_EQ(s.late_timers, mode == 0 ? 0u : 14u);
        CHECK_EQ(s.missed, s.late_timers);
        uint64_t left = 0;
        REQUIRE(embed_sched_remaining(&s, 0, &left));
        CHECK_EQ(left, 1u); // 次の期限 101 まで 1 tick
    }
}

TEST_CASE(bucket_collisions_fire_only_on_their_own_round) {
    embed_sched s;
    embed_sched_init(&s, 5, nullptr, nullptr);
    Log a{&s, {}}, b{&s, {}}, c{&s, {}};
    // 6, 70, 134 はすべて同じバケット (mod 64)
    REQUIRE(embed_sched_start(&s, 0, 1, 0, record, &a));
    REQUIRE(embed_sched_start(&s, 1, 65, 0, record, &b));
    REQUIRE(embed_sched_start(&s, 2, 129, 0, record, &c));
    step_to(s, 200);
    REQUIRE_EQ(a.at.size(), 1u);
    REQUIRE_EQ(b.at.size(), 1u);
    REQUIRE_EQ(c.at.size(), 1u);
    CHECK_EQ(a.at[0], 6u);
    CHECK_EQ(b.at[0], 70u);
    CHECK_EQ(c.at[0], 134u);
    // delay = 64 は現在と同じバケットに入るが、1 周後に発火する
    Log d{&s, {}};
    REQUIRE(embed_sched_start(&s, 3, 64, 0, record, &d));
    step_to(s, 263);
    CHECK(d.at.empty());
    step_to(s, 264);
    CHECK_EQ(d.at.size(), 1u);
}

TEST_CASE(same_tick_timers_all_fire) {
    embed_sched s;
    embed_sched_init(&s, 0, nullptr, nullptr);
    Log log{&s, {}};
    for (unsigned id = 0; id < EMBED_SCHED_TIMERS; ++id)
        REQUIRE(embed_sched_start(&s, id, 9, 0, record, &log));
    step_to(s, 9);
    CHECK_EQ(log.at.size(), (size_t)EMBED_SCHED_TIMERS);
}

namespace {
struct Backoff {
    embed_sched *s;
    uint64_t delay;
    std::vector<uint64_t> at;
};
void backoff(void *u) {
    auto *b = static_cast<Backoff *>(u);
    b->at.push_back(b->s->now);
    b->delay *= 2;
    embed_sched_start(b->s, 5, b->delay, 0, backoff, b); // 自分自身を別の遅延で再設定
}
} // namespace

TEST_CASE(callback_can_reschedule_itself_with_new_delay) {
    embed_sched s;
    embed_sched_init(&s, 0, nullptr, nullptr);
    Backoff b{&s, 1, {}};
    REQUIRE(embed_sched_start(&s, 5, 1, 0, backoff, &b));
    step_to(s, 40);
    const uint64_t expect[] = {1, 3, 7, 15, 31}; // 1, +2, +4, +8, +16
    REQUIRE_EQ(b.at.size(), 5u);
    for (size_t i = 0; i < 5; ++i)
        CHECK_EQ(b.at[i], expect[i]);
}

TEST_CASE(restart_replaces_previous_schedule) {
    embed_sched s;
    embed_sched_init(&s, 0, nullptr, nullptr);
    Log log{&s, {}};
    REQUIRE(embed_sched_start(&s, 0, 5, 0, record, &log));
    REQUIRE(embed_sched_start(&s, 0, 10, 0, record, &log));
    step_to(s, 20);
    REQUIRE_EQ(log.at.size(), 1u);
    CHECK_EQ(log.at[0], 10u);
    embed_sched_cancel(&s, 0); // 停止中の取消は無害
    embed_sched_cancel(&s, 99);
    embed_sched_cancel(nullptr, 0);
}

TEST_CASE(queries) {
    embed_sched s;
    embed_sched_init(&s, 100, nullptr, nullptr);
    Log log{&s, {}};
    uint64_t v = 0;
    CHECK(!embed_sched_next_due(&s, &v));
    CHECK(!embed_sched_remaining(&s, 0, &v));
    REQUIRE(embed_sched_start(&s, 4, 30, 0, record, &log));
    REQUIRE(embed_sched_start(&s, 9, 12, 0, record, &log));
    CHECK(embed_sched_is_active(&s, 4));
    CHECK(!embed_sched_is_active(&s, 5));
    CHECK(!embed_sched_is_active(&s, EMBED_SCHED_TIMERS));
    CHECK(!embed_sched_is_active(nullptr, 4));
    REQUIRE(embed_sched_next_due(&s, &v));
    CHECK_EQ(v, 112u);
    REQUIRE(embed_sched_remaining(&s, 4, &v));
    CHECK_EQ(v, 30u);
    step_to(s, 111);
    REQUIRE(embed_sched_remaining(&s, 9, &v));
    CHECK_EQ(v, 1u);
    embed_sched_cancel(&s, 9);
    REQUIRE(embed_sched_next_due(&s, &v));
    CHECK_EQ(v, 130u);
    CHECK(!embed_sched_remaining(&s, 4, nullptr));
    CHECK(!embed_sched_next_due(&s, nullptr));
    CHECK(!embed_sched_next_due(nullptr, &v));
}

TEST_CASE(large_time_jump_is_fast_and_exact) {
    embed_sched s;
    embed_sched_init(&s, 0, nullptr, nullptr);
    Log periodic{&s, {}}, once{&s, {}};
    REQUIRE(embed_sched_start(&s, 0, 100000000u, 100000000u, record, &periodic));
    REQUIRE(embed_sched_start(&s, 1, 777777777u, 0, record, &once));
    // tick ごとに回すと 10 億回になるが、次の期限まで飛ばすので一瞬で終わる
    REQUIRE(embed_sched_advance(&s, 1000000000u));
    REQUIRE_EQ(periodic.at.size(), 10u);
    for (size_t k = 0; k < 10; ++k)
        CHECK_EQ(periodic.at[k], 100000000u * (k + 1));
    REQUIRE_EQ(once.at.size(), 1u);
    CHECK_EQ(once.at[0], 777777777u);
    CHECK_EQ(s.now, 1000000000u);
    CHECK_EQ(s.late_timers, 10u); // 期限 = 目標時刻の 1 件だけは遅れではない
    // タイマがなければ目標時刻へ直行
    embed_sched_cancel(&s, 0);
    REQUIRE(embed_sched_advance(&s, UINT64_MAX / 2));
    CHECK_EQ(s.now, UINT64_MAX / 2);
}

TEST_CASE(round_robin_and_remove_task) {
    embed_sched s;
    fake_clock = 0;
    task_cost = 0;
    order.clear();
    embed_sched_init(&s, 0, clock_fn, nullptr);
    REQUIRE(embed_sched_add_task(&s, task_a, nullptr, 0));
    REQUIRE(embed_sched_add_task(&s, task_b, nullptr, 0));
    REQUIRE(embed_sched_add_task(&s, task_c, nullptr, 0));
    for (int i = 0; i < 4; ++i)
        REQUIRE(embed_sched_run_once(&s)); // a b c a、次は b
    REQUIRE(embed_sched_remove_task(&s, task_b, nullptr)); // 次に実行予定のタスクを外す
    REQUIRE(embed_sched_run_once(&s));
    REQUIRE(embed_sched_run_once(&s));
    const int expect[] = {0, 1, 2, 0, 2, 0};
    REQUIRE_EQ(order.size(), 6u);
    for (size_t i = 0; i < 6; ++i)
        CHECK_EQ(order[i], expect[i]);
    CHECK(!embed_sched_remove_task(&s, task_b, nullptr)); // もうない
    CHECK(!embed_sched_remove_task(&s, task_a, &s));      // user が違えば別タスク
    REQUIRE(embed_sched_remove_task(&s, task_c, nullptr));
    REQUIRE(embed_sched_remove_task(&s, task_a, nullptr));
    CHECK_EQ(s.task_count, 0u);
    CHECK(embed_sched_run_once(&s)); // タスクなしでもタイマ処理は成功
    CHECK(!embed_sched_remove_task(nullptr, task_a, nullptr));
}

TEST_CASE(task_capacity_and_validation) {
    embed_sched s;
    embed_sched_init(&s, 0, clock_fn, nullptr);
    for (unsigned i = 0; i < EMBED_SCHED_TASKS; ++i)
        CHECK(embed_sched_add_task(&s, task_a, nullptr, 0));
    CHECK(!embed_sched_add_task(&s, task_a, nullptr, 0));
    CHECK(!embed_sched_add_task(&s, nullptr, nullptr, 0));
    CHECK(!embed_sched_add_task(nullptr, task_a, nullptr, 0));
    Log log{&s, {}};
    CHECK(!embed_sched_start(&s, EMBED_SCHED_TIMERS, 1, 0, record, &log));
    CHECK(!embed_sched_start(&s, 0, 1, 0, nullptr, &log));
    CHECK(!embed_sched_start(&s, 0, 0, 0, record, &log));
    CHECK(!embed_sched_start(nullptr, 0, 1, 0, record, &log));
    embed_sched no_clock;
    embed_sched_init(&no_clock, 0, nullptr, nullptr);
    CHECK(!embed_sched_run_once(&no_clock));
    CHECK(!embed_sched_run_once(nullptr));
    CHECK(!embed_sched_advance(nullptr, 1));
    embed_sched_init(nullptr, 0, nullptr, nullptr);
}

TEST_CASE(budget_boundary_counts_overruns_only_above_budget) {
    embed_sched s;
    fake_clock = 0;
    order.clear();
    embed_sched_init(&s, 0, clock_fn, nullptr);
    REQUIRE(embed_sched_add_task(&s, task_a, nullptr, 5));
    task_cost = 5; // ちょうど予算: 超過ではない
    REQUIRE(embed_sched_run_once(&s));
    CHECK_EQ(s.task_overruns, 0u);
    task_cost = 6;
    REQUIRE(embed_sched_run_once(&s));
    CHECK_EQ(s.task_overruns, 1u);
    CHECK_EQ(s.missed, 1u);
    CHECK_EQ(s.late_timers, 0u);
    // 予算 0 は監視しない
    embed_sched t;
    embed_sched_init(&t, fake_clock, clock_fn, nullptr);
    REQUIRE(embed_sched_add_task(&t, task_a, nullptr, 0));
    task_cost = 1000;
    REQUIRE(embed_sched_run_once(&t));
    CHECK_EQ(t.task_overruns, 0u);
    task_cost = 0;
    // タスク中に時計が戻ったら失敗を返す
    embed_sched u;
    fake_clock = 50;
    embed_sched_init(&u, 50, clock_fn, nullptr);
    REQUIRE(embed_sched_add_task(&u, task_back, nullptr, 0));
    CHECK(!embed_sched_run_once(&u));
    CHECK(!u.running); // 失敗しても再入防止フラグは戻す
}

TEST_CASE(delay_overflow_near_end_of_time) {
    embed_sched s;
    embed_sched_init(&s, UINT64_MAX - 5, nullptr, nullptr);
    Log log{&s, {}};
    CHECK(!embed_sched_start(&s, 0, 6, 0, record, &log));
    REQUIRE(embed_sched_start(&s, 0, 5, 3, record, &log)); // 再登録が溢れるので 1 回で止まる
    REQUIRE(embed_sched_advance(&s, UINT64_MAX));
    REQUIRE_EQ(log.at.size(), 1u);
    CHECK_EQ(log.at[0], UINT64_MAX);
    CHECK(!embed_sched_is_active(&s, 0));
}

EMBED_TEST_MAIN()
