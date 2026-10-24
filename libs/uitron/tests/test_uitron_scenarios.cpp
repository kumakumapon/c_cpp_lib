// 複数タスクが協調動作する、運用に近いシナリオテスト。
//
// 各タスクは実際のアプリケーションと同じく、周期実行・メッセージ受信待ち・資源待ちなどで
// ブロックしながら動く(ブロッキング呼出しは E_WBLK を返し、続きは次のステップで実行する)。
// 検証するのは個々のAPIの戻り値ではなく、「いつ・どの順序で・何回」各タスクが動いたかである。
#include "uitron_sim.hpp"

using namespace sim;

enum Ev {
    EV_RUN = 1,
    EV_SEND,
    EV_RECV,
    EV_DONE,
    EV_BLOCKED,
    EV_LOG,
    EV_ACQ,
    EV_REL,
    EV_WOKE,
    EV_HANDLER,
    EV_WORK,
    EV_LED,
    EV_JOB,
};

// =============================================================================
// 1. 周期タスク: 周期の異なる3タスクが、それぞれの周期を守り、同時刻には優先度順に動く
// =============================================================================
namespace periodic {
constexpr ID kCtrl = 1, kComm = 2, kLog = 3;

void ctrl() {
    rec(kCtrl, EV_RUN);
    dly_tsk(10);
}
void comm() {
    rec(kComm, EV_RUN);
    dly_tsk(25);
}
void logger() {
    rec(kLog, EV_RUN);
    dly_tsk(50);
}
} // namespace periodic

TEST_CASE(periodic_tasks_keep_their_rate_and_run_in_priority_order) {
    using namespace periodic;
    reset();
    // 優先度は周期の短い順(レートモノトニック)。生成順は優先度の逆にして、ID順では動かないことを確認する。
    spawn(kLog, 4, logger);
    spawn(kComm, 3, comm);
    spawn(kCtrl, 2, ctrl);
    run_until_idle(); // t=0
    run_for(100);

    CHECK_SEQ(ticks_of(kCtrl, EV_RUN), 0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100);
    CHECK_SEQ(ticks_of(kComm, EV_RUN), 0, 25, 50, 75, 100);
    CHECK_SEQ(ticks_of(kLog, EV_RUN), 0, 50, 100);

    // 3タスクが同時に起床する時刻は、優先度の高い順に実行される
    for (uint32_t t : {0u, 50u, 100u})
        CHECK_SEQ(tasks_at(t, EV_RUN), kCtrl, kComm, kLog);

    // 実行後は全員が時間待ちに入り、残り時間が次の周期と一致する
    T_RTSK r = ref(kCtrl);
    CHECK(r.tskstat == TTS_WAI && r.tskwait == TTW_DLY);
    CHECK_EQ(r.lefttmo, 10u);
    r = ref(kComm);
    CHECK(r.tskstat == TTS_WAI && r.tskwait == TTW_DLY);
    CHECK_EQ(r.lefttmo, 25u);
    r = ref(kLog);
    CHECK(r.tskstat == TTS_WAI && r.tskwait == TTW_DLY);
    CHECK_EQ(r.lefttmo, 50u);
}

// =============================================================================
// 2. センサ → 処理 → ログのパイプライン
//    メモリプール(2ブロック)・メールボックス・イベントフラグを組み合わせる。
//    処理が遅い(15ms)ため、センサ(10ms周期)はプール枯渇でブロックし、
//    処理側がブロックを返却した時点で再開する(バックプレッシャー)。
// =============================================================================
namespace pipeline {
constexpr ID kSensor = 1, kProc = 2, kLogger = 3; // 優先度: 処理 > センサ > ログ
constexpr ID kPool = 1, kMbx = 1, kFlg = 1;
constexpr int kTotal = 6;

struct Packet {
    T_MSG head;
    int seq;
};
uint8_t g_pool_mem[sizeof(Packet) * 2 + 16];

// センサ
enum class SensorPhase { Get, Send, Pace } g_sensor_phase;
int g_sent;
VP g_blk;

// 処理
enum class ProcPhase { Recv, Got, Done } g_proc_phase;
T_MSG *g_msg;
Packet *g_cur;

// ログ
bool g_logger_primed;
UINT g_logger_ptn; // 待ちの出力先は、待ちが解除されるまで有効でなければならない(ローカル変数は不可)

void reset_state() {
    g_sensor_phase = SensorPhase::Get;
    g_sent = 0;
    g_blk = nullptr;
    g_proc_phase = ProcPhase::Recv;
    g_msg = nullptr;
    g_cur = nullptr;
    g_logger_primed = false;
}

void sensor() {
    switch (g_sensor_phase) {
    case SensorPhase::Get:
        if (g_sent == kTotal) {
            ext_tsk();
            return;
        }
        g_sensor_phase = SensorPhase::Send;
        if (get_mpf(kPool, &g_blk) == E_WBLK) { // プール枯渇: 処理側の返却を待つ
            rec(kSensor, EV_BLOCKED);
            return;
        }
        [[fallthrough]];
    case SensorPhase::Send: {
        Packet *p = static_cast<Packet *>(g_blk);
        p->seq = g_sent++;
        rec(kSensor, EV_SEND, p->seq);
        snd_mbx(kMbx, &p->head); // 高優先度の処理タスクを起こす
        g_sensor_phase = SensorPhase::Pace;
        dly_tsk(10); // 起こした処理タスクではなく、自タスクが周期待ちに入ること
        return;
    }
    case SensorPhase::Pace:
        g_sensor_phase = SensorPhase::Get;
        sensor();
        return;
    }
}

void processor() {
    switch (g_proc_phase) {
    case ProcPhase::Done:
        rec(kProc, EV_DONE, g_cur->seq);
        rel_mpf(kPool, g_cur); // 待っているセンサがいれば、ブロックが直接渡される
        set_flg(kFlg, 0x1);
        g_proc_phase = ProcPhase::Recv;
        [[fallthrough]];
    case ProcPhase::Recv:
        g_proc_phase = ProcPhase::Got;
        if (rcv_mbx(kMbx, &g_msg) == E_WBLK)
            return;
        [[fallthrough]];
    case ProcPhase::Got:
        g_cur = reinterpret_cast<Packet *>(g_msg);
        rec(kProc, EV_RECV, g_cur->seq);
        g_proc_phase = ProcPhase::Done;
        dly_tsk(15); // 処理時間
        return;
    }
}

void logger() {
    if (g_logger_primed)
        rec(kLogger, EV_LOG);
    g_logger_primed = true;
    wai_flg(kFlg, 0x1, TWF_ANDW | TWF_CLR, &g_logger_ptn);
}
} // namespace pipeline

TEST_CASE(sensor_pipeline_delivers_in_order_and_recovers_from_pool_exhaustion) {
    using namespace pipeline;
    reset();
    reset_state();

    T_CMPF cmpf{};
    cmpf.mpfatr = TA_TFIFO;
    cmpf.blkcnt = 2;
    cmpf.blksz = sizeof(Packet);
    cmpf.mpf = g_pool_mem;
    REQUIRE(cre_mpf(kPool, &cmpf) == E_OK);
    T_CMBX cmbx{};
    cmbx.mbxatr = TA_TFIFO | TA_MFIFO;
    REQUIRE(cre_mbx(kMbx, &cmbx) == E_OK);
    T_CFLG cflg{};
    cflg.flgatr = TA_WSGL;
    REQUIRE(cre_flg(kFlg, &cflg) == E_OK);

    spawn(kLogger, 5, logger);
    spawn(kSensor, 4, sensor);
    spawn(kProc, 2, processor);
    run_until_idle();
    run_for(100);

    // センサ: 10ms周期。プールが空になる t=40,55 ではブロックし、返却された t=45,60 に再開する
    CHECK_SEQ(ticks_of(kSensor, EV_SEND), 0, 10, 20, 30, 45, 60);
    CHECK_SEQ(ticks_of(kSensor, EV_BLOCKED), 40, 55);
    // 処理: 15msずつ、受信した順に処理する(欠落・追い越しなし)
    CHECK_SEQ(args_of(kProc, EV_RECV), 0, 1, 2, 3, 4, 5);
    CHECK_SEQ(ticks_of(kProc, EV_RECV), 0, 15, 30, 45, 60, 75);
    CHECK_SEQ(ticks_of(kProc, EV_DONE), 15, 30, 45, 60, 75, 90);
    // ログ: 処理完了のたびに、同じ時刻に1回ずつ起動する
    CHECK_SEQ(ticks_of(kLogger, EV_LOG), 15, 30, 45, 60, 75, 90);

    // 終了後: ブロックは全て返却され、メールボックスは空、センサは終了している
    T_RMPF rmpf{};
    REQUIRE(ref_mpf(kPool, &rmpf) == E_OK);
    CHECK_EQ(rmpf.fblkcnt, 2u);
    CHECK_EQ(rmpf.wtskid, TSK_SELF);
    T_RMBX rmbx{};
    REQUIRE(ref_mbx(kMbx, &rmbx) == E_OK);
    CHECK(rmbx.pk_msg == nullptr);
    CHECK(ref(kSensor).tskstat == TTS_DMT);
    CHECK(ref(kProc).tskwait == TTW_MBX); // 次のメッセージを待っている
    CHECK(ref(kLogger).tskwait == TTW_FLG);
}

// =============================================================================
// 3. 共有資源(セマフォ)の待ち行列: 低優先度タスクが資源を保持している間に、
//    中優先度(t=5)→高優先度(t=8)の順に獲得を要求する。
//    TA_TPRI なら優先度順、TA_TFIFO なら到着順に資源が渡される。
// =============================================================================
namespace contention {
constexpr ID kHolder = 6, kMid = 5, kHigh = 2; // IDは到着順・優先度順のどちらとも一致させない
constexpr ID kSem = 1;

enum class Phase { Arrive, Request, Hold, Release };

struct Waiter {
    Phase phase = Phase::Arrive;
    uint32_t arrive;
    uint32_t hold;
};
Waiter g_w[8];

void reset_state() {
    g_w[kHolder] = {Phase::Request, 0, 20};
    g_w[kMid] = {Phase::Arrive, 5, 3};
    g_w[kHigh] = {Phase::Arrive, 8, 3};
}

template <ID N> void task() {
    Waiter &w = g_w[N];
    switch (w.phase) {
    case Phase::Arrive:
        w.phase = Phase::Request;
        dly_tsk(w.arrive);
        return;
    case Phase::Request:
        w.phase = Phase::Hold;
        if (wai_sem(kSem) == E_WBLK)
            return;
        [[fallthrough]];
    case Phase::Hold:
        rec(N, EV_ACQ);
        w.phase = Phase::Release;
        dly_tsk(w.hold);
        return;
    case Phase::Release:
        rec(N, EV_REL);
        sig_sem(kSem); // 待ちタスクがいれば直接渡る
        ext_tsk();
        return;
    }
}

std::vector<int> acquisition_order(ATR sematr) {
    reset();
    reset_state();
    T_CSEM cs{};
    cs.sematr = sematr;
    cs.isemcnt = 1;
    cs.maxsem = 1;
    CHECK(cre_sem(kSem, &cs) == E_OK);
    spawn(kHolder, 6, task<kHolder>);
    spawn(kMid, 4, task<kMid>);
    spawn(kHigh, 2, task<kHigh>);
    run_until_idle();
    run_for(40);

    std::vector<int> order;
    for (const Rec &r : g_log)
        if (r.ev == EV_ACQ)
            order.push_back(r.task);
    return order;
}
} // namespace contention

TEST_CASE(semaphore_handoff_follows_priority_when_TA_TPRI) {
    using namespace contention;
    auto order = acquisition_order(TA_TPRI);
    CHECK_SEQ(order, kHolder, kHigh, kMid);
    // 保持者の解放(t=20)で高優先度へ、その解放(t=23)で中優先度へ渡る
    CHECK_SEQ(ticks_of(kHigh, EV_ACQ), 20);
    CHECK_SEQ(ticks_of(kMid, EV_ACQ), 23);
    // 全員が解放した後、資源は返却されている
    T_RSEM rs{};
    REQUIRE(ref_sem(kSem, &rs) == E_OK);
    CHECK_EQ(rs.semcnt, 1u);
    CHECK_EQ(rs.wtskid, TSK_SELF);
}

TEST_CASE(semaphore_handoff_follows_arrival_order_when_TA_TFIFO) {
    using namespace contention;
    auto order = acquisition_order(TA_TFIFO);
    // 到着順: 中優先度(t=5) → 高優先度(t=8)。優先度やタスクIDでは決まらない
    CHECK_SEQ(order, kHolder, kMid, kHigh);
    CHECK_SEQ(ticks_of(kMid, EV_ACQ), 20);
    CHECK_SEQ(ticks_of(kHigh, EV_ACQ), 23);
}

// =============================================================================
// 4. イベントフラグによる合流(ランデブー): 3つのワーカーの完了を、調整役が AND 待ちする。
// =============================================================================
namespace rendezvous {
constexpr ID kCoord = 1, kW1 = 2, kW2 = 3, kW3 = 4;
constexpr ID kFlg = 1;
constexpr UINT kSentinel = 0xDEAD;

UINT g_ptn;
bool g_coord_primed;
uint32_t g_delay[kW3 + 1];
bool g_worker_started[kW3 + 1];

void reset_state(uint32_t d1, uint32_t d2, uint32_t d3) {
    g_ptn = kSentinel;
    g_coord_primed = false;
    g_delay[kW1] = d1;
    g_delay[kW2] = d2;
    g_delay[kW3] = d3;
    for (bool &b : g_worker_started)
        b = false;
}

void coordinator() {
    if (g_coord_primed) { // 待ちが解除された(成立またはタイムアウト)
        rec(kCoord, EV_WOKE, uitron_wait_result(TSK_SELF)); // 成立 E_OK / タイムアウト E_TMOUT
        rec(kCoord, EV_LOG, g_ptn == kSentinel ? -1 : static_cast<int>(g_ptn));
        ext_tsk();
        return;
    }
    g_coord_primed = true;
    twai_flg(kFlg, 0x7, TWF_ANDW | TWF_CLR, &g_ptn, 100);
}

template <ID N> void worker() {
    if (!g_worker_started[N]) {
        g_worker_started[N] = true;
        dly_tsk(g_delay[N]); // 作業時間
        return;
    }
    rec(N, EV_DONE);
    set_flg(kFlg, 1u << (N - kW1)); // 調整役(高優先度)を起こす場合がある
    ext_tsk();                      // 自タスクが終了すること
}

void setup(uint32_t d1, uint32_t d2, uint32_t d3) {
    reset();
    reset_state(d1, d2, d3);
    T_CFLG cflg{};
    cflg.flgatr = TA_WSGL;
    CHECK(cre_flg(kFlg, &cflg) == E_OK);
    spawn(kCoord, 1, coordinator);
    spawn(kW1, 3, worker<kW1>);
    spawn(kW2, 3, worker<kW2>);
    spawn(kW3, 3, worker<kW3>);
    run_until_idle();
}

UINT flag_now() {
    T_RFLG r{};
    CHECK(ref_flg(kFlg, &r) == E_OK);
    return r.flgptn;
}
} // namespace rendezvous

TEST_CASE(flag_rendezvous_wakes_coordinator_when_the_last_worker_finishes) {
    using namespace rendezvous;
    setup(20, 35, 50);
    T_RTSK r = ref(kCoord);
    CHECK(r.tskstat == TTS_WAI && r.tskwait == TTW_FLG);

    for (uint32_t t = 1; t <= 60; ++t) {
        tick();
        if (t == 20) // 1つ目完了: まだ合流していない
            CHECK_EQ(flag_now(), 0x1u);
        if (t == 35)
            CHECK_EQ(flag_now(), 0x3u);
    }
    // 最後の完了(t=50)と同じ時刻に調整役が起床し、フラグは TWF_CLR でクリアされる
    CHECK_SEQ(ticks_of(kW1, EV_DONE), 20);
    CHECK_SEQ(ticks_of(kW2, EV_DONE), 35);
    CHECK_SEQ(ticks_of(kW3, EV_DONE), 50);
    CHECK_SEQ(ticks_of(kCoord, EV_WOKE), 50);
    CHECK_SEQ(args_of(kCoord, EV_WOKE), E_OK);
    CHECK_SEQ(args_of(kCoord, EV_LOG), 0x7);
    CHECK_EQ(flag_now(), 0u);
    // 全員終了
    for (ID id : {kCoord, kW1, kW2, kW3})
        CHECK(ref(id).tskstat == TTS_DMT);
}

TEST_CASE(flag_rendezvous_times_out_when_a_worker_is_late) {
    using namespace rendezvous;
    setup(20, 35, 150); // 3つ目が遅い
    run_for(99);
    CHECK(ticks_of(kCoord, EV_WOKE).empty());
    run_for(1);

    // 待ち開始(t=0)から100ms後にタイムアウトで起床する。成立時のパターンは書き込まれない
    CHECK_SEQ(ticks_of(kCoord, EV_WOKE), 100);
    CHECK_SEQ(args_of(kCoord, EV_WOKE), E_TMOUT);
    CHECK_SEQ(args_of(kCoord, EV_LOG), -1);
    // タイムアウト時点で揃っているのは 0x3 で、遅れたワーカー(bit2)を特定できる。TWF_CLR は働かない
    CHECK_EQ(flag_now(), 0x3u);

    run_for(50);
    // 遅れたワーカーの完了(t=150)は誰も待っていないので、フラグに残る
    CHECK_SEQ(ticks_of(kW3, EV_DONE), 150);
    CHECK_EQ(flag_now(), 0x7u);
}

// =============================================================================
// 5. 割込みハンドラ → ハンドラタスク → ワーカー → LED のチェーン。
//    割込み(iset_flg / iwup_tsk)は、同じ時刻内に下流まで処理が連鎖する。
// =============================================================================
namespace isr_chain {
constexpr ID kHandler = 1, kWorker = 2, kLed = 3;
constexpr ID kFlg = 1, kMbx = 1;

struct Msg {
    T_MSG head;
    int n;
};
Msg g_msgs[8];
int g_msg_used;
bool g_handler_primed, g_worker_primed, g_led_primed;
T_MSG *g_rx;
UINT g_ptn;

void reset_state() {
    g_msg_used = 0;
    g_handler_primed = g_worker_primed = g_led_primed = false;
    g_rx = nullptr;
    g_ptn = 0;
}

void handler() { // 割込みフラグを待ち、ワーカーを起こす
    if (g_handler_primed) {
        rec(kHandler, EV_HANDLER);
        wup_tsk(kWorker);
    }
    g_handler_primed = true;
    wai_flg(kFlg, 0x1, TWF_ANDW | TWF_CLR, &g_ptn);
}

void worker() { // 起床要求のたびに1回作業し、結果をLEDタスクへ送る
    if (g_worker_primed) {
        Msg &m = g_msgs[g_msg_used];
        m.n = g_msg_used++;
        rec(kWorker, EV_WORK, m.n);
        snd_mbx(kMbx, &m.head);
    }
    g_worker_primed = true;
    slp_tsk(); // 起床要求が溜まっていれば即座に戻る
}

void led() {
    if (g_led_primed)
        rec(kLed, EV_LED, reinterpret_cast<Msg *>(g_rx)->n);
    g_led_primed = true;
    rcv_mbx(kMbx, &g_rx);
}

void isr(uint32_t t) {
    if (t == 7)
        iset_flg(kFlg, 0x1);
    if (t == 12) { // ハンドラが動く前に2回目の割込みが入っても、フラグが残るので取りこぼさない
        iset_flg(kFlg, 0x1);
        iset_flg(kFlg, 0x1);
    }
    if (t == 20) { // 起床要求は溜まる(取りこぼさない)
        iwup_tsk(kWorker);
        iwup_tsk(kWorker);
    }
}
} // namespace isr_chain

TEST_CASE(interrupt_events_propagate_through_the_task_chain_within_the_same_tick) {
    using namespace isr_chain;
    reset();
    reset_state();
    T_CFLG cflg{};
    cflg.flgatr = TA_WSGL;
    REQUIRE(cre_flg(kFlg, &cflg) == E_OK);
    T_CMBX cmbx{};
    cmbx.mbxatr = TA_TFIFO | TA_MFIFO;
    REQUIRE(cre_mbx(kMbx, &cmbx) == E_OK);
    spawn(kLed, 5, led);
    spawn(kWorker, 3, worker);
    spawn(kHandler, 1, handler);
    g_isr = isr;
    run_until_idle();
    run_for(30);

    // 割込み(t=7)の処理は、ハンドラ → ワーカー → LED の順に、遅延なく同じ時刻に完了する
    CHECK_SEQ(ticks_of(kHandler, EV_HANDLER), 7, 12, 12);
    CHECK_SEQ(ticks_of(kWorker, EV_WORK), 7, 12, 12, 20, 20);
    CHECK_SEQ(ticks_of(kLed, EV_LED), 7, 12, 12, 20, 20);
    // 受信順序は送信順序のまま
    CHECK_SEQ(args_of(kLed, EV_LED), 0, 1, 2, 3, 4);
    // 同じ時刻の中でも、ハンドラ → ワーカー → LED の順に連鎖する
    CHECK_SEQ(tasks_at(7, EV_HANDLER), kHandler);
    CHECK_SEQ(tasks_at(7, EV_WORK), kWorker);
    CHECK_SEQ(tasks_at(7, EV_LED), kLed);
    CHECK_SEQ(tasks_at(20, EV_WORK), kWorker, kWorker);
    // 最終的に全タスクが待ちに戻り、起床要求・フラグは残らない
    CHECK(ref(kHandler).tskwait == TTW_FLG);
    CHECK(ref(kWorker).tskwait == TTW_SLP);
    CHECK_EQ(ref(kWorker).wupcnt, 0u);
    CHECK(ref(kLed).tskwait == TTW_MBX);
}

// =============================================================================
// 6. 優先度とスケジューリング
// =============================================================================
namespace sched {
constexpr ID kHog = 1, kLow = 2;
int g_hog_steps, g_low_steps, g_burst;

void hog() { // 5ステップ連続して動いた後、10ms休む
    ++g_hog_steps;
    if (++g_burst == 5) {
        g_burst = 0;
        dly_tsk(10);
    }
}
void low() { ++g_low_steps; }
} // namespace sched

TEST_CASE(high_priority_task_starves_lower_ones_until_it_blocks) {
    using namespace sched;
    reset();
    g_hog_steps = g_low_steps = g_burst = 0;
    spawn(kLow, 6, low);
    spawn(kHog, 2, hog);

    run_steps(20);
    // hog が待ちに入る(5ステップ目)まで、低優先度は一度も動かない
    CHECK_EQ(g_hog_steps, 5);
    CHECK_EQ(g_low_steps, 15);

    for (int i = 0; i < 9; ++i)
        uitron_os_tick(1);
    run_steps(3);
    CHECK_EQ(g_hog_steps, 5); // まだ休止中
    CHECK_EQ(g_low_steps, 18);

    uitron_os_tick(1); // t=10: hog が起床
    run_steps(5);      // 起床直後から、再び低優先度を押しのけて5ステップ動く
    CHECK_EQ(g_hog_steps, 10);
    CHECK_EQ(g_low_steps, 18);
    run_steps(2);
    CHECK_EQ(g_low_steps, 20);
}

namespace prio {
constexpr ID kA = 1, kB = 2;
int g_a, g_b;
void a() { ++g_a; }
void b() { ++g_b; }
} // namespace prio

TEST_CASE(priority_changes_take_effect_at_the_next_dispatch) {
    using namespace prio;
    reset();
    g_a = g_b = 0;
    spawn(kA, 3, a);
    spawn(kB, 4, b);

    run_steps(4);
    CHECK_EQ(g_a, 4);
    CHECK_EQ(g_b, 0);

    REQUIRE(chg_pri(kB, 1) == E_OK); // B を A より高くする
    run_steps(4);
    CHECK_EQ(g_a, 4);
    CHECK_EQ(g_b, 4);
    CHECK_EQ(ref(kB).tskbpri, 4);
    CHECK_EQ(ref(kB).tskpri, 1);

    REQUIRE(chg_pri(kB, 0) == E_OK); // ベース優先度へ戻す
    run_steps(4);
    CHECK_EQ(g_a, 8);
    CHECK_EQ(g_b, 4);
}

// =============================================================================
// 7. 起動要求のキューイング: 割込みからの起動要求を、タスクが1件ずつ処理する
// =============================================================================
namespace jobq {
constexpr ID kWorker = 1;
int g_jobs;

void worker() {
    rec(kWorker, EV_JOB, g_jobs++);
    ext_tsk(); // 起動要求が溜まっていれば、そのまま次のジョブとして再起動される
}

void isr(uint32_t t) {
    if (t == 5) { // バースト: 3件同時
        iact_tsk(kWorker);
        iact_tsk(kWorker);
        iact_tsk(kWorker);
    }
    if (t == 8)
        iact_tsk(kWorker);
}
} // namespace jobq

TEST_CASE(queued_activation_requests_run_as_separate_jobs) {
    using namespace jobq;
    reset();
    g_jobs = 0;
    T_CTSK c{};
    c.tskatr = TA_HLNG; // 休止状態で生成し、割込みで起動する
    c.itskpri = 3;
    c.task = worker;
    c.stksz = 256;
    REQUIRE(cre_tsk(kWorker, &c) == E_OK);
    g_isr = isr;
    run_until_idle();
    CHECK(ref(kWorker).tskstat == TTS_DMT);

    run_for(12);
    // バースト3件は同じ時刻に3回、ジョブとして順に実行される。取りこぼし・重複はない
    CHECK_SEQ(ticks_of(kWorker, EV_JOB), 5, 5, 5, 8);
    CHECK_SEQ(args_of(kWorker, EV_JOB), 0, 1, 2, 3);
    T_RTSK r = ref(kWorker);
    CHECK(r.tskstat == TTS_DMT);
    CHECK_EQ(r.actcnt, 0u);
}

// =============================================================================
// 8. 監視タスクによるタスク制御: サスペンド・強制終了・待ち解除・オブジェクト削除
// =============================================================================
namespace supervise {
constexpr ID kSup = 1, kWorker = 2, kWorker2 = 3;
constexpr ID kSem = 1;

struct Step {
    uint32_t wait;
    void (*act)();
};
const Step *g_script;
size_t g_script_len, g_script_pos;
bool g_script_started;
bool g_worker_primed, g_worker2_primed;

void run_script() { // 待ち→操作 を繰り返す。操作は、監視タスク自身が動いているときに実行する
    if (g_script_started && g_script_pos < g_script_len)
        g_script[g_script_pos++].act();
    g_script_started = true;
    if (g_script_pos < g_script_len)
        dly_tsk(g_script[g_script_pos].wait);
    else
        ext_tsk();
}

void periodic_worker() {
    rec(kWorker, EV_RUN);
    dly_tsk(10);
}

void sem_waiter() {
    if (g_worker_primed) { // 待ちが解除された
        rec(kWorker, EV_WOKE);
        ext_tsk();
        return;
    }
    g_worker_primed = true;
    wai_sem(kSem);
}
void sem_waiter2() {
    if (g_worker2_primed) {
        rec(kWorker2, EV_WOKE);
        ext_tsk();
        return;
    }
    g_worker2_primed = true;
    wai_sem(kSem);
}
void sleeper() {
    if (g_worker_primed)
        rec(kWorker, EV_WOKE);
    g_worker_primed = true;
    slp_tsk();
}

void setup(const Step *script, size_t n) {
    reset();
    g_script = script;
    g_script_len = n;
    g_script_pos = 0;
    g_script_started = false;
    g_worker_primed = g_worker2_primed = false;
}
} // namespace supervise

TEST_CASE(suspended_waiting_task_stays_suspended_after_its_delay_expires) {
    using namespace supervise;
    static const Step script[] = {
        {5, [] { CHECK(sus_tsk(kWorker) == E_OK); }},
        {25, [] { CHECK(rsm_tsk(kWorker) == E_OK); }},
    };
    setup(script, 2);
    spawn(kSup, 1, run_script);
    spawn(kWorker, 4, periodic_worker);
    run_until_idle(); // t=0: worker 実行 → 待ち(10ms)

    for (uint32_t t = 1; t <= 35; ++t) {
        tick();
        if (t == 5) // 時間待ち中にサスペンド → 二重待ち
            CHECK(ref(kWorker).tskstat == TTS_WAS);
        if (t == 10 || t == 29) { // 待ち時間が満了しても、サスペンドは維持される
            T_RTSK r = ref(kWorker);
            CHECK(r.tskstat == TTS_SUS);
            CHECK_EQ(r.suscnt, 1u);
        }
    }
    // t=30 の再開で初めて動き、その後は元の周期(10ms)で動く
    CHECK_SEQ(ticks_of(kWorker, EV_RUN), 0, 30);
    run_for(5);
    CHECK_SEQ(ticks_of(kWorker, EV_RUN), 0, 30, 40);
}

TEST_CASE(terminated_waiter_does_not_swallow_the_released_resource) {
    using namespace supervise;
    static const Step script[] = {
        {5, [] { CHECK(ter_tsk(kWorker) == E_OK); }},
        {5, [] { CHECK(sig_sem(kSem) == E_OK); }},
    };
    setup(script, 2);
    T_CSEM cs{};
    cs.sematr = TA_TFIFO;
    cs.isemcnt = 0;
    cs.maxsem = 1;
    REQUIRE(cre_sem(kSem, &cs) == E_OK);
    spawn(kSup, 1, run_script);
    spawn(kWorker, 4, sem_waiter);
    run_until_idle();
    CHECK(ref(kWorker).tskwait == TTW_SEM);

    run_for(4);
    CHECK(ref(kWorker).tskstat == TTS_WAI);
    run_for(1); // t=5: 待ち中のワーカーを強制終了
    CHECK(ref(kWorker).tskstat == TTS_DMT);
    run_for(5); // t=10: 資源を返却 → 待ち手がいないので、資源として残る
    T_RSEM rs{};
    REQUIRE(ref_sem(kSem, &rs) == E_OK);
    CHECK_EQ(rs.semcnt, 1u);
    CHECK_EQ(rs.wtskid, TSK_SELF);
    CHECK(ticks_of(kWorker, EV_WOKE).empty()); // 終了済みのタスクは動かない
}

TEST_CASE(deleting_a_semaphore_releases_every_waiter_at_once) {
    using namespace supervise;
    static const Step script[] = {
        {8, [] { CHECK(del_sem(kSem) == E_OK); }},
    };
    setup(script, 1);
    T_CSEM cs{};
    cs.sematr = TA_TFIFO;
    cs.isemcnt = 0;
    cs.maxsem = 1;
    REQUIRE(cre_sem(kSem, &cs) == E_OK);
    spawn(kSup, 1, run_script);
    spawn(kWorker, 4, sem_waiter);
    spawn(kWorker2, 5, sem_waiter2);
    run_until_idle();
    CHECK(ref(kWorker).tskwait == TTW_SEM);
    CHECK(ref(kWorker2).tskwait == TTW_SEM);

    run_for(7);
    CHECK(g_log.empty()); // 削除前は誰も動かない
    run_for(1);
    // 削除と同じ時刻に、待っていた全員が優先度順に動く
    CHECK_SEQ(ticks_of(kWorker, EV_WOKE), 8);
    CHECK_SEQ(ticks_of(kWorker2, EV_WOKE), 8);
    CHECK_SEQ(tasks_at(8, EV_WOKE), kWorker, kWorker2);
}

TEST_CASE(forced_release_wakes_a_sleeping_task_but_wakeup_requests_are_queued) {
    using namespace supervise;
    static const Step script[] = {
        {7, [] { CHECK(rel_wai(kWorker) == E_OK); }},   // 待ち強制解除 → 即起床
        {5, [] { CHECK(wup_tsk(kWorker) == E_OK); }},   // 起床要求 (ワーカーは再び待ち中)
        {5, [] { CHECK(wup_tsk(kWorker) == E_OK); }},   // 待ち中でなければ溜まる
    };
    setup(script, 3);
    spawn(kSup, 1, run_script);
    spawn(kWorker, 4, sleeper);
    run_until_idle();
    CHECK(ref(kWorker).tskwait == TTW_SLP);

    run_for(30);
    // 強制解除(t=7) → 再び待つ → 起床要求(t=12) → 再び待つ → 起床要求(t=17)
    CHECK_SEQ(ticks_of(kWorker, EV_WOKE), 7, 12, 17);
    CHECK(ref(kWorker).tskwait == TTW_SLP);
}

// =============================================================================
// 9. ブロッキング系サービスコール: タスクの中から呼ぶと E_WBLK を返して待ちを登録し、
//    タイムアウトで次のステップから続きを実行する(全種類の待ち要因)
// =============================================================================
namespace blocking {
constexpr ID kTask = 1, kSem = 1, kFlg = 1, kMbx = 1, kMpf = 1;
int g_step;
UINT g_ptn;
T_MSG *g_msg;
VP g_blk;

void task() {
    if (g_step > 0) // 前回の待ちの結果。5件はタイムアウト、最後の dly_tsk は時間経過なので E_OK
        rec(kTask, EV_RUN, uitron_wait_result(TSK_SELF));
    ER er = E_OK;
    switch (g_step) {
    case 0: er = twai_sem(kSem, 5); break;
    case 1: er = twai_flg(kFlg, 0x1, TWF_ANDW, &g_ptn, 5); break;
    case 2: er = trcv_mbx(kMbx, &g_msg, 5); break;
    case 3: er = tget_mpf(kMpf, &g_blk, 5); break;
    case 4: er = tslp_tsk(5); break;
    case 5: er = dly_tsk(5); break;
    default: ext_tsk(); return;
    }
    T_RTSK r{};
    ref_tsk(TSK_SELF, &r);
    rec(kTask, EV_BLOCKED, er);
    rec(kTask, EV_SEND, uitron_wait_result(TSK_SELF)); // 待ち中は E_WBLK
    rec(kTask, EV_LOG, static_cast<int>(r.tskwait));
    rec(kTask, EV_WOKE, static_cast<int>(r.tskstat));
    ++g_step;
}
} // namespace blocking

TEST_CASE(blocking_calls_register_a_wait_and_resume_after_the_timeout) {
    using namespace blocking;
    reset();
    g_step = 0;
    g_ptn = 0;
    g_msg = nullptr;
    g_blk = nullptr;

    T_CSEM cs{};
    cs.sematr = TA_TFIFO;
    cs.isemcnt = 0;
    cs.maxsem = 1;
    REQUIRE(cre_sem(kSem, &cs) == E_OK);
    T_CFLG cf{};
    cf.flgatr = TA_WSGL;
    REQUIRE(cre_flg(kFlg, &cf) == E_OK);
    T_CMBX cm{};
    cm.mbxatr = TA_TFIFO | TA_MFIFO;
    REQUIRE(cre_mbx(kMbx, &cm) == E_OK);
    static uint8_t pool[64];
    T_CMPF cp{};
    cp.mpfatr = TA_TFIFO;
    cp.blkcnt = 1;
    cp.blksz = 32;
    cp.mpf = pool;
    REQUIRE(cre_mpf(kMpf, &cp) == E_OK);
    VP taken = nullptr;
    REQUIRE(pget_mpf(kMpf, &taken) == E_OK); // 唯一のブロックを使い切っておく

    spawn(kTask, 3, task);
    run_until_idle();
    run_for(35);

    // 5ms待つたびに次のステップが実行され、6種類の待ちを順に経る
    CHECK_SEQ(ticks_of(kTask, EV_BLOCKED), 0, 5, 10, 15, 20, 25);
    // どれも成功(E_OK)ではなく、待ちを登録した(E_WBLK)ことを返す
    CHECK_SEQ(args_of(kTask, EV_BLOCKED), E_WBLK, E_WBLK, E_WBLK, E_WBLK, E_WBLK, E_WBLK);
    // 待ち要因は呼び出したサービスコールに対応し、状態は待ち(WAITING)である
    CHECK_SEQ(args_of(kTask, EV_LOG), TTW_SEM, TTW_FLG, TTW_MBX, TTW_MPF, TTW_SLP, TTW_DLY);
    CHECK_SEQ(args_of(kTask, EV_WOKE), TTS_WAI, TTS_WAI, TTS_WAI, TTS_WAI, TTS_WAI, TTS_WAI);
    CHECK_SEQ(args_of(kTask, EV_SEND), E_WBLK, E_WBLK, E_WBLK, E_WBLK, E_WBLK, E_WBLK);
    // 再開したステップでは、待ちの結果を戻り値として得られる(タイムアウト×5、時間経過 E_OK)
    CHECK_SEQ(args_of(kTask, EV_RUN), E_TMOUT, E_TMOUT, E_TMOUT, E_TMOUT, E_TMOUT, E_OK);
    // タイムアウトした待ちは、成立したパターン・メッセージ・ブロックを書き込まない
    CHECK_EQ(g_ptn, 0u);
    CHECK(g_msg == nullptr);
    CHECK(g_blk == nullptr);
    // 最後の待ち(t=30)の後でタスクは終了する
    CHECK(ref(kTask).tskstat == TTS_DMT);
}

// =============================================================================
// 10. 待ちの結果: 成立・タイムアウト・強制解除・オブジェクト削除を、再開したタスクが区別できる
// =============================================================================
namespace results {
constexpr ID kSup = 1, kTimeout = 2, kDeleted = 3, kReleased = 4, kSignaled = 5;
bool g_primed[8];

template <ID N> void waiter() {
    if (g_primed[N]) { // 待ちが解除された。その理由を取得する
        rec(N, EV_WOKE, uitron_wait_result(TSK_SELF));
        ext_tsk();
        return;
    }
    g_primed[N] = true;
    switch (N) {
    case kTimeout: twai_sem(1, 20); break;  // 誰も返却しない → タイムアウト
    case kDeleted: wai_sem(2); break;       // セマフォ削除
    case kReleased: slp_tsk(); break;       // rel_wai で強制解除
    case kSignaled: wai_sem(3); break;      // sig_sem で成立
    }
}
} // namespace results

TEST_CASE(wait_result_tells_why_a_blocked_task_resumed) {
    using namespace results;
    static const supervise::Step script[] = {
        {10, [] { CHECK(rel_wai(kReleased) == E_OK); }},
        {5, [] { CHECK(sig_sem(3) == E_OK); }},
        {15, [] { CHECK(del_sem(2) == E_OK); }},
    };
    supervise::setup(script, 3);
    for (bool &b : g_primed)
        b = false;
    for (ID sid : {1, 2, 3}) {
        T_CSEM cs{};
        cs.sematr = TA_TFIFO;
        cs.isemcnt = 0;
        cs.maxsem = 1;
        REQUIRE(cre_sem(sid, &cs) == E_OK);
    }
    spawn(kSup, 1, supervise::run_script);
    spawn(kTimeout, 3, waiter<kTimeout>);
    spawn(kDeleted, 3, waiter<kDeleted>);
    spawn(kReleased, 3, waiter<kReleased>);
    spawn(kSignaled, 3, waiter<kSignaled>);
    run_until_idle();

    // 待っている間は E_WBLK
    for (ID id : {kTimeout, kDeleted, kReleased, kSignaled})
        CHECK_EQ(uitron_wait_result(id), E_WBLK);

    run_for(40);
    CHECK_SEQ(ticks_of(kReleased, EV_WOKE), 10);
    CHECK_SEQ(args_of(kReleased, EV_WOKE), E_RLWAI);
    CHECK_SEQ(ticks_of(kSignaled, EV_WOKE), 15);
    CHECK_SEQ(args_of(kSignaled, EV_WOKE), E_OK);
    CHECK_SEQ(ticks_of(kTimeout, EV_WOKE), 20);
    CHECK_SEQ(args_of(kTimeout, EV_WOKE), E_TMOUT);
    CHECK_SEQ(ticks_of(kDeleted, EV_WOKE), 30);
    CHECK_SEQ(args_of(kDeleted, EV_WOKE), E_DLT);
    // タスクが終了(休止)すると結果は消去され、再起動した実行に前回の結果を持ち越さない
    CHECK(ref(kTimeout).tskstat == TTS_DMT);
    CHECK_EQ(uitron_wait_result(kTimeout), E_OK);
    CHECK_EQ(uitron_wait_result(kDeleted), E_OK);
}

EMBED_TEST_MAIN()
