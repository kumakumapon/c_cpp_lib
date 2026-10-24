#pragma once
// uitron のシナリオテスト用ヘルパー。
//
// uitron はホスト/協調実行向けで、タスクのエントリ関数は「1ステップ」として
// uitron_os_run_step() から繰り返し呼ばれる。ブロッキング系サービスコールは待ちを登録して
// E_WBLK を返すので、タスクはその場で return し、待ちが解除された後の次のステップで続きを実行する。
// そのためシナリオ内のタスクは、フェーズ変数を持つステップ関数(状態機械)として書く。
//
// シミュレーションの時間は、1 tick = 1 ms とし、sim_tick() で進める。
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <type_traits>
#include <vector>

#include "embed_test/embed_test.hpp"
#include "uitron/itron.h"
#include "uitron/kernel.h"
#include "uitron/uitron_os.h"

namespace sim {

// ---- イベントログ -----------------------------------------------------------
struct Rec {
    uint32_t tick;
    int task;
    int ev;
    int arg;
};

inline std::vector<Rec> g_log;

inline uint32_t now() {
    SYSTIM t = 0;
    get_tim(&t);
    return static_cast<uint32_t>(t);
}

inline void rec(int task, int ev, int arg = 0) { g_log.push_back({now(), task, ev, arg}); }

// 指定タスク・イベントが記録された時刻の一覧
inline std::vector<uint32_t> ticks_of(int task, int ev) {
    std::vector<uint32_t> v;
    for (const Rec &r : g_log)
        if (r.task == task && r.ev == ev)
            v.push_back(r.tick);
    return v;
}

// 指定タスク・イベントの引数の一覧
inline std::vector<int> args_of(int task, int ev) {
    std::vector<int> v;
    for (const Rec &r : g_log)
        if (r.task == task && r.ev == ev)
            v.push_back(r.arg);
    return v;
}

// 指定時刻に記録されたイベントのタスクID(記録順)
inline std::vector<int> tasks_at(uint32_t tick, int ev) {
    std::vector<int> v;
    for (const Rec &r : g_log)
        if (r.tick == tick && r.ev == ev)
            v.push_back(r.task);
    return v;
}

template <class T>
bool same_seq(const std::vector<T> &actual, const std::vector<T> &expected, const char *expr,
              const char *file, int line) {
    bool ok = actual == expected;
    if (!::embed_test::check(ok, expr, file, line)) {
        std::printf("# expected:");
        for (const T &x : expected)
            std::printf(" %lld", static_cast<long long>(x));
        std::printf("\n# actual:  ");
        for (const T &x : actual)
            std::printf(" %lld", static_cast<long long>(x));
        std::printf("\n");
    }
    return ok;
}

// ---- 実行ループ -------------------------------------------------------------
// 割込みハンドラ相当。tick を進めた直後、タスクを動かす前に呼ばれる。
using IsrHook = void (*)(uint32_t tick);
inline IsrHook g_isr = nullptr;

inline void reset() {
    uitron_os_init();
    g_log.clear();
    g_isr = nullptr;
}

// 実行可能なタスクが無くなるまでステップ実行する。
// どのタスクも待ちに入らずに動き続ける場合(無限ループ)を検出するため、上限を設ける。
inline bool run_until_idle(unsigned guard = 256) {
    for (unsigned i = 0; i < guard; ++i) {
        uitron_os_schedule();
        if (uitron_os_get_current_task() == TSK_SELF)
            return true;
        uitron_os_run_step();
    }
    ::embed_test::check(false, "run_until_idle: tasks never became idle", __FILE__, __LINE__);
    return false;
}

// 指定回数だけステップ実行する(待ちに入らないタスクがある場合の優先度検証用)。
inline void run_steps(unsigned n) {
    for (unsigned i = 0; i < n; ++i) {
        uitron_os_schedule();
        if (uitron_os_get_current_task() == TSK_SELF)
            return;
        uitron_os_run_step();
    }
}

// 1ms 進め、割込みハンドラ → タスクの順に、全タスクが待ちに入るまで実行する。
inline void tick() {
    uitron_os_tick(1);
    if (g_isr)
        g_isr(now());
    run_until_idle();
}

inline void run_for(uint32_t ms) {
    for (uint32_t i = 0; i < ms; ++i)
        tick();
}

// ---- タスク生成 -------------------------------------------------------------
inline void spawn(ID id, PRI pri, void (*entry)(void)) {
    T_CTSK c{};
    c.tskatr = TA_HLNG | TA_ACT;
    c.itskpri = pri;
    c.task = entry;
    c.stksz = 256;
    ::embed_test::check(cre_tsk(id, &c) == E_OK, "spawn: cre_tsk", __FILE__, __LINE__);
}

inline T_RTSK ref(ID id) {
    T_RTSK r{};
    ::embed_test::check(ref_tsk(id, &r) == E_OK, "ref_tsk", __FILE__, __LINE__);
    return r;
}

} // namespace sim

#define CHECK_SEQ(actual, ...)                                                                     \
    ::sim::same_seq((actual), std::decay_t<decltype(actual)>{__VA_ARGS__}, #actual, __FILE__,      \
                    __LINE__)
