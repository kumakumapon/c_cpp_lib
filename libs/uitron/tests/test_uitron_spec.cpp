// μITRON 4.0 仕様に沿ったサービスコールの引数検証 (E_ID / E_NOEXS / E_PAR / E_OBJ / E_CTX)、
// 生成パラメータ、待ち行列の整合、タイムアウト、メールボックスのメッセージ優先度、
// 二重待ち状態の扱いを固定するテスト。タスクの本体は実行せず、API の戻り値と状態だけを見る。
#include "embed_test/embed_test.hpp"
#include <cstdint>
#include <cstring>
#include <initializer_list>

#include "uitron/itron.h"
#include "uitron/kernel.h"
#include "uitron/uitron_os.h"

namespace {
constexpr ID kMaxTsk = UITRON_MAX_TSKID;
constexpr ID kMaxSem = UITRON_MAX_SEMID;
constexpr ID kMaxFlg = UITRON_MAX_FLGID;
constexpr ID kMaxMbx = UITRON_MAX_MBXID;
constexpr ID kMaxMpf = UITRON_MAX_MPFID;

void nop() {}

void fresh() { uitron_os_init(); }

// pri の小さい (高優先度) ものから順に作ると、作った順に実行状態になり、待ちに入る順序を作れる
void mk(ID id, PRI pri, ATR atr = TA_HLNG | TA_ACT) {
    T_CTSK c{};
    c.tskatr = atr;
    c.itskpri = pri;
    c.task = nop;
    c.stksz = 128;
    REQUIRE(cre_tsk(id, &c) == E_OK);
}

void mk_sem(ID id, UINT init, UINT max, ATR atr = TA_TFIFO) {
    T_CSEM s{atr, init, max};
    REQUIRE(cre_sem(id, &s) == E_OK);
}
void mk_flg(ID id, ATR atr = TA_WSGL, UINT init = 0) {
    T_CFLG f{atr, init};
    REQUIRE(cre_flg(id, &f) == E_OK);
}
void mk_mbx(ID id, ATR atr = TA_MFIFO, PRI maxpri = 4) {
    T_CMBX m{atr, maxpri, nullptr};
    REQUIRE(cre_mbx(id, &m) == E_OK);
}
void mk_mpf(ID id, UINT cnt, UINT sz, ATR atr = TA_TFIFO, VP mem = nullptr) {
    T_CMPF p{atr, cnt, sz, mem};
    REQUIRE(cre_mpf(id, &p) == E_OK);
}

T_RTSK rt(ID id) {
    T_RTSK r{};
    CHECK(ref_tsk(id, &r) == E_OK);
    return r;
}
} // namespace

// ---- 引数検証 ----
TEST_CASE(id_out_of_range_is_E_ID_for_every_service_call) {
    fresh();
    T_CSEM cs{TA_TFIFO, 0, 1};
    T_CFLG cf{TA_WSGL, 0};
    T_CMBX cm{TA_MFIFO, 1, nullptr};
    T_CMPF cp{TA_TFIFO, 1, 8, nullptr};
    T_CTSK ct{};
    ct.itskpri = 1;
    ct.task = nop;
    T_RSEM rs;
    T_RFLG rf;
    T_RMBX rm;
    T_RMPF rp;
    T_RTSK rtk;
    T_MSG msg;
    T_MSG *pm = nullptr;
    VP blk = &msg;
    UINT ptn = 0;
    PRI pri = 0;

    for (ID bad : {-1, 0, 17, 1000}) {
        CHECK_EQ(cre_sem(bad, &cs), E_ID);
        CHECK_EQ(del_sem(bad), E_ID);
        CHECK_EQ(sig_sem(bad), E_ID);
        CHECK_EQ(isig_sem(bad), E_ID);
        CHECK_EQ(wai_sem(bad), E_ID);
        CHECK_EQ(pol_sem(bad), E_ID);
        CHECK_EQ(twai_sem(bad, 5), E_ID);
        CHECK_EQ(ref_sem(bad, &rs), E_ID);

        CHECK_EQ(cre_flg(bad, &cf), E_ID);
        CHECK_EQ(del_flg(bad), E_ID);
        CHECK_EQ(set_flg(bad, 1), E_ID);
        CHECK_EQ(iset_flg(bad, 1), E_ID);
        CHECK_EQ(clr_flg(bad, 1), E_ID);
        CHECK_EQ(wai_flg(bad, 1, TWF_ORW, &ptn), E_ID);
        CHECK_EQ(pol_flg(bad, 1, TWF_ORW, &ptn), E_ID);
        CHECK_EQ(twai_flg(bad, 1, TWF_ORW, &ptn, 5), E_ID);
        CHECK_EQ(ref_flg(bad, &rf), E_ID);

        CHECK_EQ(cre_mbx(bad, &cm), E_ID);
        CHECK_EQ(del_mbx(bad), E_ID);
        CHECK_EQ(snd_mbx(bad, &msg), E_ID);
        CHECK_EQ(isnd_mbx(bad, &msg), E_ID);
        CHECK_EQ(rcv_mbx(bad, &pm), E_ID);
        CHECK_EQ(prcv_mbx(bad, &pm), E_ID);
        CHECK_EQ(trcv_mbx(bad, &pm, 5), E_ID);
        CHECK_EQ(ref_mbx(bad, &rm), E_ID);

        CHECK_EQ(cre_mpf(bad, &cp), E_ID);
        CHECK_EQ(del_mpf(bad), E_ID);
        CHECK_EQ(get_mpf(bad, &blk), E_ID);
        CHECK_EQ(pget_mpf(bad, &blk), E_ID);
        CHECK_EQ(tget_mpf(bad, &blk, 5), E_ID);
        CHECK_EQ(rel_mpf(bad, &msg), E_ID);
        CHECK_EQ(ref_mpf(bad, &rp), E_ID);
    }
    // タスクは 0 が自タスク指定なので、負と上限超過 (0 は別のテスト)
    for (ID bad : {-1, 17, 1000}) {
        CHECK_EQ(cre_tsk(bad, &ct), E_ID);
        CHECK_EQ(del_tsk(bad), E_ID);
        CHECK_EQ(act_tsk(bad), E_ID);
        CHECK_EQ(iact_tsk(bad), E_ID);
        CHECK_EQ(can_act(bad), E_ID);
        CHECK_EQ(ter_tsk(bad), E_ID);
        CHECK_EQ(chg_pri(bad, 1), E_ID);
        CHECK_EQ(get_pri(bad, &pri), E_ID);
        CHECK_EQ(ref_tsk(bad, &rtk), E_ID);
        CHECK_EQ(wup_tsk(bad), E_ID);
        CHECK_EQ(iwup_tsk(bad), E_ID);
        CHECK_EQ(can_wup(bad), E_ID);
        CHECK_EQ(rel_wai(bad), E_ID);
        CHECK_EQ(irel_wai(bad), E_ID);
        CHECK_EQ(sus_tsk(bad), E_ID);
        CHECK_EQ(rsm_tsk(bad), E_ID);
    }
    // 上限ちょうどは有効な ID (未生成なので E_NOEXS)
    CHECK_EQ(del_sem(kMaxSem), E_NOEXS);
    CHECK_EQ(del_flg(kMaxFlg), E_NOEXS);
    CHECK_EQ(del_mbx(kMaxMbx), E_NOEXS);
    CHECK_EQ(del_mpf(kMaxMpf), E_NOEXS);
    CHECK_EQ(del_tsk(kMaxTsk), E_NOEXS);
    CHECK_EQ(sig_sem(1), E_NOEXS);
}

TEST_CASE(task_self_without_a_running_task_is_E_ID) {
    fresh();
    PRI pri = 0;
    T_RTSK r;
    CHECK_EQ(act_tsk(TSK_SELF), E_ID);
    CHECK_EQ(ter_tsk(TSK_SELF), E_ID);
    CHECK_EQ(chg_pri(TSK_SELF, 1), E_ID);
    CHECK_EQ(get_pri(TSK_SELF, &pri), E_ID);
    CHECK_EQ(ref_tsk(TSK_SELF, &r), E_ID);
    CHECK_EQ(wup_tsk(TSK_SELF), E_ID);
    CHECK_EQ(sus_tsk(TSK_SELF), E_ID);
}

TEST_CASE(not_created_objects_are_E_NOEXS) {
    fresh();
    T_RSEM rs;
    T_RFLG rf;
    T_RMBX rm;
    T_RMPF rp;
    T_RTSK rtk;
    T_MSG msg, *pm = nullptr;
    VP blk = nullptr;
    UINT ptn = 0;
    PRI pri = 0;
    CHECK_EQ(del_sem(3), E_NOEXS);
    CHECK_EQ(sig_sem(3), E_NOEXS);
    CHECK_EQ(pol_sem(3), E_NOEXS);
    CHECK_EQ(twai_sem(3, 5), E_NOEXS);
    CHECK_EQ(ref_sem(3, &rs), E_NOEXS);
    CHECK_EQ(del_flg(3), E_NOEXS);
    CHECK_EQ(set_flg(3, 1), E_NOEXS);
    CHECK_EQ(clr_flg(3, 1), E_NOEXS);
    CHECK_EQ(pol_flg(3, 1, TWF_ORW, &ptn), E_NOEXS);
    CHECK_EQ(twai_flg(3, 1, TWF_ORW, &ptn, 5), E_NOEXS);
    CHECK_EQ(ref_flg(3, &rf), E_NOEXS);
    CHECK_EQ(del_mbx(3), E_NOEXS);
    CHECK_EQ(snd_mbx(3, &msg), E_NOEXS);
    CHECK_EQ(prcv_mbx(3, &pm), E_NOEXS);
    CHECK_EQ(trcv_mbx(3, &pm, 5), E_NOEXS);
    CHECK_EQ(ref_mbx(3, &rm), E_NOEXS);
    CHECK_EQ(del_mpf(3), E_NOEXS);
    CHECK_EQ(pget_mpf(3, &blk), E_NOEXS);
    CHECK_EQ(tget_mpf(3, &blk, 5), E_NOEXS);
    CHECK_EQ(rel_mpf(3, &msg), E_NOEXS);
    CHECK_EQ(ref_mpf(3, &rp), E_NOEXS);
    CHECK_EQ(del_tsk(3), E_NOEXS);
    CHECK_EQ(act_tsk(3), E_NOEXS);
    CHECK_EQ(can_act(3), E_NOEXS);
    CHECK_EQ(ter_tsk(3), E_NOEXS);
    CHECK_EQ(chg_pri(3, 1), E_NOEXS);
    CHECK_EQ(get_pri(3, &pri), E_NOEXS);
    CHECK_EQ(ref_tsk(3, &rtk), E_NOEXS);
    CHECK_EQ(wup_tsk(3), E_NOEXS);
    CHECK_EQ(can_wup(3), E_NOEXS);
    CHECK_EQ(rel_wai(3), E_NOEXS);
    CHECK_EQ(sus_tsk(3), E_NOEXS);
    CHECK_EQ(rsm_tsk(3), E_NOEXS);

    // 削除した後も E_NOEXS
    mk_sem(3, 0, 1);
    CHECK_EQ(del_sem(3), E_OK);
    CHECK_EQ(sig_sem(3), E_NOEXS);
    CHECK_EQ(del_sem(3), E_NOEXS);
}

TEST_CASE(null_pointers_are_E_PAR) {
    fresh();
    mk_sem(1, 0, 1);
    mk_flg(1);
    mk_mbx(1);
    mk_mpf(1, 2, 16);
    mk(1, 5, TA_HLNG);
    UINT ptn = 0;
    CHECK_EQ(cre_sem(2, nullptr), E_PAR);
    CHECK_EQ(cre_flg(2, nullptr), E_PAR);
    CHECK_EQ(cre_mbx(2, nullptr), E_PAR);
    CHECK_EQ(cre_mpf(2, nullptr), E_PAR);
    CHECK_EQ(cre_tsk(2, nullptr), E_PAR);
    CHECK_EQ(ref_sem(1, nullptr), E_PAR);
    CHECK_EQ(ref_flg(1, nullptr), E_PAR);
    CHECK_EQ(ref_mbx(1, nullptr), E_PAR);
    CHECK_EQ(ref_mpf(1, nullptr), E_PAR);
    CHECK_EQ(ref_tsk(1, nullptr), E_PAR);
    CHECK_EQ(get_pri(1, nullptr), E_PAR);
    CHECK_EQ(snd_mbx(1, nullptr), E_PAR);
    CHECK_EQ(rcv_mbx(1, nullptr), E_PAR);
    CHECK_EQ(prcv_mbx(1, nullptr), E_PAR);
    CHECK_EQ(trcv_mbx(1, nullptr, 5), E_PAR);
    CHECK_EQ(get_mpf(1, nullptr), E_PAR);
    CHECK_EQ(pget_mpf(1, nullptr), E_PAR);
    CHECK_EQ(tget_mpf(1, nullptr, 5), E_PAR);
    CHECK_EQ(rel_mpf(1, nullptr), E_PAR);
    CHECK_EQ(set_tim(nullptr), E_PAR);
    CHECK_EQ(get_tim(nullptr), E_PAR);
    // フラグ待ちの出力先 (p_flgptn) は省略できる。待ちパターン 0 は不正
    CHECK_EQ(pol_flg(1, 1, TWF_ORW, nullptr), E_TMOUT);
    CHECK_EQ(pol_flg(1, 0, TWF_ORW, &ptn), E_PAR);
    CHECK_EQ(pol_flg(1, 0, TWF_ANDW, &ptn), E_PAR);
}

TEST_CASE(create_parameter_validation) {
    fresh();
    // セマフォ: 最大値 0 は不可、初期値は最大値以下
    T_CSEM s{TA_TFIFO, 0, 0};
    CHECK_EQ(cre_sem(1, &s), E_PAR);
    s = {TA_TFIFO, 3, 2};
    CHECK_EQ(cre_sem(1, &s), E_PAR);
    s = {TA_TFIFO, 2, 2};
    CHECK_EQ(cre_sem(1, &s), E_OK);
    s = {TA_TFIFO, 0, 1};
    CHECK_EQ(cre_sem(1, &s), E_OBJ); // 二重生成
    T_RSEM rs;
    ref_sem(1, &rs);
    CHECK_EQ(rs.semcnt, 2u);
    s = {TA_TFIFO, 0, 1};
    CHECK_EQ(cre_sem(kMaxSem, &s), E_OK); // 上限の ID も作れる

    // メモリプール: ブロック数・サイズ 0 は不可。内蔵領域に収まらないと E_NOMEM
    T_CMPF p{TA_TFIFO, 0, 8, nullptr};
    CHECK_EQ(cre_mpf(1, &p), E_PAR);
    p = {TA_TFIFO, 4, 0, nullptr};
    CHECK_EQ(cre_mpf(1, &p), E_PAR);
    p = {TA_TFIFO, 3, 1024, nullptr}; // 3072 > 2048
    CHECK_EQ(cre_mpf(1, &p), E_NOMEM);
    p = {TA_TFIFO, 2, 1024, nullptr}; // ちょうど 2048
    CHECK_EQ(cre_mpf(1, &p), E_OK);
    p = {TA_TFIFO, 1, 8, nullptr};
    CHECK_EQ(cre_mpf(1, &p), E_OBJ);
    const UINT fit = 2048 / sizeof(void *); // 1 バイトは 1 ワードに切り上げて数える
    p = {TA_TFIFO, fit + 1, 1, nullptr};
    CHECK_EQ(cre_mpf(2, &p), E_NOMEM);
    p = {TA_TFIFO, fit, 1, nullptr}; // ちょうど 2048 バイト
    CHECK_EQ(cre_mpf(2, &p), E_OK);

    // タスク: 優先度は 1..UITRON_MAX_PRI
    T_CTSK t{};
    t.task = nop;
    for (PRI bad : {-1, 0, UITRON_MAX_PRI + 1}) {
        t.itskpri = bad;
        CHECK_EQ(cre_tsk(1, &t), E_PAR);
    }
    t.itskpri = 1;
    CHECK_EQ(cre_tsk(1, &t), E_OK);
    t.itskpri = UITRON_MAX_PRI;
    CHECK_EQ(cre_tsk(2, &t), E_OK);
    CHECK_EQ(cre_tsk(2, &t), E_OBJ);

    // メールボックスの優先度順は、最大メッセージ優先度が 1 以上であること
    T_CMBX m{TA_MPRI, 0, nullptr};
    CHECK_EQ(cre_mbx(1, &m), E_PAR);
    m = {TA_MPRI, 4, nullptr};
    CHECK_EQ(cre_mbx(1, &m), E_OK);
    CHECK_EQ(cre_mbx(1, &m), E_OBJ);
    T_CFLG f{TA_WSGL, 0};
    CHECK_EQ(cre_flg(1, &f), E_OK);
    CHECK_EQ(cre_flg(1, &f), E_OBJ);
}

TEST_CASE(task_state_errors_are_E_OBJ) {
    fresh();
    mk(1, 5, TA_HLNG); // 休止状態
    mk(2, 6);          // 実行可能
    PRI pri;
    CHECK_EQ(ter_tsk(1), E_OBJ);
    CHECK_EQ(get_pri(1, &pri), E_OBJ);
    CHECK_EQ(chg_pri(1, 3), E_OBJ);
    CHECK_EQ(wup_tsk(1), E_OBJ);
    CHECK_EQ(can_wup(1), E_OBJ);
    CHECK_EQ(sus_tsk(1), E_OBJ);
    CHECK_EQ(rel_wai(1), E_OBJ);
    CHECK_EQ(rsm_tsk(1), E_OBJ);
    CHECK_EQ(del_tsk(2), E_OBJ); // 休止状態でないと削除できない
    CHECK_EQ(rel_wai(2), E_OBJ); // 待ち状態でない
    CHECK_EQ(rsm_tsk(2), E_OBJ); // 強制待ち状態でない
    CHECK_EQ(del_tsk(1), E_OK);
}

TEST_CASE(blocking_calls_outside_a_task_are_E_CTX) {
    fresh();
    mk_sem(1, 0, 2);
    mk_flg(1);
    mk_mbx(1);
    mk_mpf(1, 1, 8);
    T_MSG *pm = nullptr;
    VP blk = nullptr;
    UINT ptn = 0;
    // 実行状態のタスクがない: 待ちが必要な呼び出しは E_CTX、待ちが不要なら成功する
    CHECK_EQ(uitron_os_get_current_task(), TSK_SELF);
    CHECK_EQ(wai_sem(1), E_CTX);
    CHECK_EQ(twai_sem(1, 5), E_CTX);
    CHECK_EQ(wai_flg(1, 1, TWF_ORW, &ptn), E_CTX);
    CHECK_EQ(twai_flg(1, 1, TWF_ORW, &ptn, 5), E_CTX);
    CHECK_EQ(rcv_mbx(1, &pm), E_CTX);
    CHECK_EQ(trcv_mbx(1, &pm, 5), E_CTX);
    CHECK_EQ(slp_tsk(), E_CTX);
    CHECK_EQ(tslp_tsk(5), E_CTX);
    CHECK_EQ(dly_tsk(5), E_CTX);
    CHECK_EQ(pol_sem(1), E_TMOUT);
    CHECK_EQ(pol_flg(1, 1, TWF_ORW, &ptn), E_TMOUT);
    CHECK_EQ(prcv_mbx(1, &pm), E_TMOUT);
    CHECK_EQ(tslp_tsk(TMO_POL), E_CTX);
    CHECK_EQ(dly_tsk(0), E_CTX);

    CHECK_EQ(get_mpf(1, &blk), E_OK); // 空きがあれば待たずに成功
    CHECK_EQ(get_mpf(1, &blk), E_CTX);
    CHECK_EQ(pget_mpf(1, &blk), E_TMOUT);
    CHECK_EQ(sig_sem(1), E_OK);
    CHECK_EQ(wai_sem(1), E_OK);
}

// ---- セマフォ ----
TEST_CASE(semaphore_counting_and_timeouts) {
    fresh();
    mk_sem(1, 1, 2);
    T_RSEM rs;
    CHECK_EQ(pol_sem(1), E_OK);
    CHECK_EQ(pol_sem(1), E_TMOUT);
    CHECK_EQ(sig_sem(1), E_OK);
    CHECK_EQ(sig_sem(1), E_OK);
    CHECK_EQ(sig_sem(1), E_QOVR); // 最大値超過
    CHECK_EQ(isig_sem(1), E_QOVR);
    ref_sem(1, &rs);
    CHECK_EQ(rs.semcnt, 2u);
    CHECK_EQ(rs.wtskid, TSK_SELF); // 待ちなし

    mk(1, 1);
    mk(2, 2);
    CHECK_EQ(pol_sem(1), E_OK);
    CHECK_EQ(pol_sem(1), E_OK);
    // タスク 1 が待ち (1 ms)、タスク 2 が続けて待つ
    CHECK_EQ(uitron_os_get_current_task(), 1);
    CHECK_EQ(twai_sem(1, 1), E_WBLK);
    CHECK_EQ(uitron_os_get_current_task(), 2);
    CHECK_EQ(twai_sem(1, 3), E_WBLK);
    CHECK_EQ(uitron_os_get_current_task(), TSK_SELF);
    ref_sem(1, &rs);
    CHECK_EQ(rs.wtskid, 1);
    CHECK_EQ(rt(1).tskstat, TTS_WAI);
    CHECK_EQ(rt(1).tskwait, TTW_SEM);
    CHECK_EQ(rt(1).wid, 1);
    CHECK_EQ(rt(1).lefttmo, 1u);
    CHECK_EQ(uitron_wait_result(1), E_WBLK);

    uitron_os_tick(1); // 1 ms で境界のタイムアウト
    CHECK(rt(1).tskstat != TTS_WAI); // 時間切れで待ちは解除される (最高優先度なので実行状態になる)
    CHECK_EQ(uitron_wait_result(1), E_TMOUT);
    CHECK_EQ(rt(2).lefttmo, 2u); // 残りが減る
    CHECK_EQ(rt(2).tskstat, TTS_WAI);
    uitron_os_tick(1);
    CHECK_EQ(rt(2).lefttmo, 1u);
    uitron_os_tick(1);
    CHECK_EQ(uitron_wait_result(2), E_TMOUT);
}

TEST_CASE(forever_wait_never_expires_and_multi_step_tick_expires_once) {
    fresh();
    mk_sem(1, 0, 1);
    mk(1, 1);
    mk(2, 2);
    CHECK_EQ(wai_sem(1), E_WBLK);              // 永久待ち
    CHECK_EQ(rt(1).lefttmo, TMO_FEVR);
    CHECK_EQ(twai_sem(1, 7), E_WBLK);          // タスク 2: 7 ms
    uitron_os_tick(5);                         // 複数ステップをまとめて進める
    CHECK_EQ(rt(1).tskstat, TTS_WAI);
    CHECK_EQ(rt(1).lefttmo, TMO_FEVR);         // 永久待ちは減らない
    CHECK_EQ(rt(2).lefttmo, 2u);
    uitron_os_tick(100);                       // 残りを大きく超えても 1 回だけ解除
    CHECK_EQ(uitron_wait_result(2), E_TMOUT);
    CHECK_EQ(rt(1).tskstat, TTS_WAI);
    SYSTIM t = 0;
    CHECK_EQ(get_tim(&t), E_OK);
    CHECK_EQ(t, (SYSTIM)105);
    uitron_os_tick(0); // 0 ms では何も変わらない
    CHECK_EQ(rt(1).tskstat, TTS_WAI);
}

TEST_CASE(dly_tsk_completes_with_E_OK_after_the_delay) {
    fresh();
    mk(1, 1);
    CHECK_EQ(dly_tsk(0), E_OK); // 0 は待たない
    CHECK_EQ(rt(1).tskstat, TTS_RUN);
    CHECK_EQ(dly_tsk(3), E_WBLK);
    CHECK_EQ(rt(1).tskwait, TTW_DLY);
    CHECK_EQ(rt(1).lefttmo, 3u);
    uitron_os_tick(2);
    CHECK_EQ(rt(1).tskstat, TTS_WAI);
    uitron_os_tick(1);
    CHECK(rt(1).tskstat != TTS_WAI);
    CHECK_EQ(uitron_wait_result(1), E_OK); // 遅延の時間経過はタイムアウトではなく正常
}

TEST_CASE(sleep_wakeup_counters_and_wait_result) {
    fresh();
    mk(1, 1);
    CHECK_EQ(wup_tsk(1), E_OK);
    CHECK_EQ(wup_tsk(1), E_OK);
    CHECK_EQ(rt(1).wupcnt, 2u);
    CHECK_EQ(tslp_tsk(TMO_POL), E_OK); // 起床要求が溜まっていれば待たずに戻る
    CHECK_EQ(rt(1).wupcnt, 1u);
    CHECK_EQ(can_wup(1), 1);
    CHECK_EQ(can_wup(1), 0);
    CHECK_EQ(tslp_tsk(TMO_POL), E_TMOUT);
    CHECK_EQ(slp_tsk(), E_WBLK);
    CHECK_EQ(rt(1).tskwait, TTW_SLP);
    CHECK_EQ(wup_tsk(1), E_OK); // 待っていれば起こす (キューイングしない)
    CHECK_EQ(rt(1).wupcnt, 0u);
    CHECK_EQ(uitron_wait_result(1), E_OK);
    for (int i = 0; i < 255; ++i) CHECK_EQ(wup_tsk(1), E_OK);
    CHECK_EQ(wup_tsk(1), E_QOVR);
    CHECK_EQ(iwup_tsk(1), E_QOVR);
}

// ---- イベントフラグ ----
TEST_CASE(flag_conditions_and_clear_modes) {
    fresh();
    mk_flg(1, TA_WSGL, 0x0C);
    UINT p = 0;
    CHECK_EQ(pol_flg(1, 0x04, TWF_ANDW, &p), E_OK);
    CHECK_EQ(p, 0x0Cu);
    CHECK_EQ(pol_flg(1, 0x0C, TWF_ANDW, &p), E_OK);
    CHECK_EQ(pol_flg(1, 0x1C, TWF_ANDW, &p), E_TMOUT); // 1 ビット足りない
    CHECK_EQ(pol_flg(1, 0x1C, TWF_ORW, &p), E_OK);
    CHECK_EQ(pol_flg(1, 0x30, TWF_ORW, &p), E_TMOUT);
    p = 0xFFFF;
    CHECK_EQ(pol_flg(1, 0x30, TWF_ORW, &p), E_TMOUT);
    CHECK_EQ(p, 0xFFFFu); // 失敗時は出力を書き換えない

    T_RFLG rf;
    CHECK_EQ(pol_flg(1, 0x04, TWF_ANDW | TWF_CLR, &p), E_OK); // 成立でクリア
    ref_flg(1, &rf);
    CHECK_EQ(rf.flgptn, 0u);

    CHECK_EQ(set_flg(1, 0x05), E_OK);
    CHECK_EQ(set_flg(1, 0x02), E_OK);
    ref_flg(1, &rf);
    CHECK_EQ(rf.flgptn, 0x07u); // OR で積み上がる
    CHECK_EQ(clr_flg(1, 0x06), E_OK); // AND でクリア
    ref_flg(1, &rf);
    CHECK_EQ(rf.flgptn, 0x06u);
    CHECK_EQ(iset_flg(1, 0x01), E_OK);

    mk_flg(2, TA_WSGL | TA_CLR, 0x03); // TA_CLR は待ち成立のたびに全クリア
    CHECK_EQ(pol_flg(2, 0x01, TWF_ORW, &p), E_OK);
    ref_flg(2, &rf);
    CHECK_EQ(rf.flgptn, 0u);
}

TEST_CASE(flag_waiters_wmul_and_single) {
    fresh();
    mk(1, 1);
    mk(2, 2);
    mk(3, 3);
    mk_flg(1, TA_WMUL);
    UINT p1 = 0, p2 = 0, p3 = 0;
    CHECK_EQ(wai_flg(1, 0x01, TWF_ORW, &p1), E_WBLK); // タスク 1
    CHECK_EQ(wai_flg(1, 0x02, TWF_ORW, &p2), E_WBLK); // タスク 2
    CHECK_EQ(wai_flg(1, 0x04, TWF_ANDW, &p3), E_WBLK); // タスク 3
    T_RFLG rf;
    ref_flg(1, &rf);
    CHECK_EQ(rf.wtskid, 1);
    CHECK_EQ(set_flg(1, 0x03), E_OK); // 1 回のセットで 2 つ満たす (TA_WMUL)
    CHECK_EQ(rt(1).tskstat != TTS_WAI, true);
    CHECK_EQ(rt(2).tskstat != TTS_WAI, true);
    CHECK_EQ(rt(3).tskstat, TTS_WAI); // 条件未成立は待ち続ける
    CHECK_EQ(p1, 0x03u);
    CHECK_EQ(p2, 0x03u);
    CHECK_EQ(uitron_wait_result(1), E_OK);
    CHECK_EQ(set_flg(1, 0x04), E_OK);
    CHECK_EQ(p3, 0x07u);
    CHECK_EQ(rt(3).tskstat != TTS_WAI, true);
}

TEST_CASE(flag_clear_mode_with_multiple_waiters_consumes_the_pattern) {
    fresh();
    mk(1, 1);
    mk(2, 2);
    mk_flg(1, TA_WMUL);
    UINT p1 = 0, p2 = 0;
    CHECK_EQ(wai_flg(1, 0x01, TWF_ORW | TWF_CLR, &p1), E_WBLK);
    CHECK_EQ(wai_flg(1, 0x01, TWF_ORW, &p2), E_WBLK);
    CHECK_EQ(set_flg(1, 0x01), E_OK); // 先のタスクがクリアするので、後のタスクは満たされない
    CHECK_EQ(rt(1).tskstat != TTS_WAI, true);
    CHECK_EQ(rt(2).tskstat, TTS_WAI);
    T_RFLG rf;
    ref_flg(1, &rf);
    CHECK_EQ(rf.flgptn, 0u);
}

TEST_CASE(flag_single_waiter_attribute_rejects_a_second_waiter) {
    fresh();
    mk(1, 1);
    mk(2, 2);
    mk_flg(1, TA_WSGL);
    UINT p = 0;
    CHECK_EQ(wai_flg(1, 0x01, TWF_ORW, &p), E_WBLK);
    CHECK_EQ(uitron_os_get_current_task(), 2);
    CHECK_EQ(wai_flg(1, 0x02, TWF_ORW, &p), E_ILUSE); // 待ちタスクがいる TA_WSGL は不可
    CHECK_EQ(rt(2).tskstat, TTS_RUN);
}

// ---- メールボックス ----
TEST_CASE(mailbox_fifo_and_message_priority_order) {
    fresh();
    mk_mbx(1, TA_MFIFO);
    mk_mbx(2, TA_MPRI, 8);
    T_MSG_PRI m[5];
    std::memset(m, 0, sizeof(m));
    const PRI pri[5] = {3, 1, 2, 1, 3};
    T_MSG *got = nullptr;
    T_RMBX rm;

    for (int i = 0; i < 3; ++i) CHECK_EQ(snd_mbx(1, &m[i].msghead), E_OK);
    ref_mbx(1, &rm);
    CHECK(rm.pk_msg == &m[0].msghead);
    for (int i = 0; i < 3; ++i) {
        CHECK_EQ(prcv_mbx(1, &got), E_OK);
        CHECK(got == &m[i].msghead); // 送信順
    }
    CHECK_EQ(prcv_mbx(1, &got), E_TMOUT);

    for (int i = 0; i < 5; ++i) {
        m[i].msgpri = pri[i];
        CHECK_EQ(snd_mbx(2, &m[i].msghead), E_OK);
    }
    // 優先度の小さい順、同じ優先度は送信順: m1, m3, m2, m0, m4
    const int order[5] = {1, 3, 2, 0, 4};
    ref_mbx(2, &rm);
    CHECK(rm.pk_msg == &m[1].msghead);
    for (int i = 0; i < 5; ++i) {
        CHECK_EQ(prcv_mbx(2, &got), E_OK);
        CHECK(got == &m[order[i]].msghead);
    }
    CHECK_EQ(prcv_mbx(2, &got), E_TMOUT);
}

TEST_CASE(mailbox_message_priority_is_validated) {
    fresh();
    mk_mbx(1, TA_MPRI, 4);
    T_MSG_PRI m{};
    m.msgpri = 0;
    CHECK_EQ(snd_mbx(1, &m.msghead), E_PAR);
    m.msgpri = 5;
    CHECK_EQ(snd_mbx(1, &m.msghead), E_PAR);
    m.msgpri = 4;
    CHECK_EQ(snd_mbx(1, &m.msghead), E_OK);
    T_RMBX rm;
    ref_mbx(1, &rm);
    CHECK(rm.pk_msg == &m.msghead);
}

TEST_CASE(mailbox_delivers_directly_to_waiters_in_queue_order) {
    fresh();
    mk(1, 1);
    mk(2, 2);
    mk(3, 3);
    mk_mbx(1, TA_MFIFO | TA_TPRI);
    T_MSG a{}, b{}, c{};
    T_MSG *r1 = nullptr, *r2 = nullptr, *r3 = nullptr;
    CHECK_EQ(rcv_mbx(1, &r1), E_WBLK); // 1
    CHECK_EQ(rcv_mbx(1, &r2), E_WBLK); // 2
    CHECK_EQ(trcv_mbx(1, &r3, 2), E_WBLK); // 3
    T_RMBX rm;
    ref_mbx(1, &rm);
    CHECK_EQ(rm.wtskid, 1);
    CHECK(rm.pk_msg == nullptr);
    CHECK_EQ(snd_mbx(1, &a), E_OK);
    CHECK(r1 == &a);
    CHECK_EQ(uitron_wait_result(1), E_OK);
    CHECK_EQ(isnd_mbx(1, &b), E_OK);
    CHECK(r2 == &b);
    ref_mbx(1, &rm);
    CHECK_EQ(rm.wtskid, 3);
    uitron_os_tick(2);
    CHECK_EQ(uitron_wait_result(3), E_TMOUT);
    CHECK(r3 == nullptr);
    CHECK_EQ(snd_mbx(1, &c), E_OK); // 待ちがいなければキューに入る
    ref_mbx(1, &rm);
    CHECK(rm.pk_msg == &c);
    CHECK_EQ(rm.wtskid, TSK_SELF);
}

// ---- 固定長メモリプール ----
TEST_CASE(memory_pool_blocks_are_distinct_aligned_and_recycled) {
    fresh();
    alignas(16) static uint8_t area[4 * 24];
    std::memset(area, 0, sizeof(area));
    mk_mpf(1, 4, 20, TA_TFIFO, area); // 20 バイトは 8 バイト境界に切り上げて 24
    VP b[4];
    T_RMPF rp;
    for (int i = 0; i < 4; ++i) {
        CHECK_EQ(pget_mpf(1, &b[i]), E_OK);
        CHECK((uint8_t *)b[i] >= area && (uint8_t *)b[i] < area + sizeof(area));
        CHECK_EQ(((uint8_t *)b[i] - area) % sizeof(void *), 0u);
        for (int j = 0; j < i; ++j) CHECK(b[i] != b[j]);
    }
    ref_mpf(1, &rp);
    CHECK_EQ(rp.fblkcnt, 0u);
    VP x = nullptr;
    CHECK_EQ(pget_mpf(1, &x), E_TMOUT);
    CHECK(x == nullptr);
    CHECK_EQ(rel_mpf(1, b[2]), E_OK);
    ref_mpf(1, &rp);
    CHECK_EQ(rp.fblkcnt, 1u);
    CHECK_EQ(pget_mpf(1, &x), E_OK);
    CHECK(x == b[2]); // 返したブロックが再び使われる
    // ブロック間隔は切り上げたサイズ
    uint8_t *lo = area + sizeof(area), *hi = area;
    for (int i = 0; i < 4; ++i) {
        if ((uint8_t *)b[i] < lo) lo = (uint8_t *)b[i];
        if ((uint8_t *)b[i] > hi) hi = (uint8_t *)b[i];
    }
    CHECK_EQ((size_t)(hi - lo), 3 * 24u);
}

TEST_CASE(memory_pool_rejects_foreign_blocks) {
    fresh();
    alignas(16) static uint8_t area[3 * 16];
    mk_mpf(1, 3, 16, TA_TFIFO, area);
    VP b = nullptr;
    pget_mpf(1, &b);
    uint8_t outside[16];
    CHECK_EQ(rel_mpf(1, outside), E_PAR);       // プール外
    CHECK_EQ(rel_mpf(1, (uint8_t *)b + 1), E_PAR); // ブロック境界でない
    CHECK_EQ(rel_mpf(1, area + sizeof(area)), E_PAR); // 末尾の外
    T_RMPF rp;
    ref_mpf(1, &rp);
    CHECK_EQ(rp.fblkcnt, 2u); // 不正な返却で空きブロック数が増えない
    CHECK_EQ(rel_mpf(1, b), E_OK);
    ref_mpf(1, &rp);
    CHECK_EQ(rp.fblkcnt, 3u);
}

TEST_CASE(memory_pool_hands_blocks_to_waiters_by_attribute) {
    fresh();
    mk(1, 1);
    mk(2, 2);
    mk(3, 3);
    mk_mpf(1, 1, 8, TA_TPRI);
    VP held = nullptr;
    CHECK_EQ(pget_mpf(1, &held), E_OK);
    VP w1 = nullptr, w2 = nullptr, w3 = nullptr;
    CHECK_EQ(get_mpf(1, &w1), E_WBLK);       // 1
    CHECK_EQ(get_mpf(1, &w2), E_WBLK);       // 2
    CHECK_EQ(tget_mpf(1, &w3, 4), E_WBLK);   // 3
    CHECK_EQ(chg_pri(1, 5), E_OK);           // 待ち中の優先度変更で順序が入れ替わる (TA_TPRI)
    CHECK_EQ(chg_pri(3, 1), E_OK);
    T_RMPF rp;
    CHECK_EQ(rel_mpf(1, held), E_OK);
    CHECK(w3 == held);
    CHECK(w1 == nullptr);
    CHECK_EQ(uitron_wait_result(3), E_OK);
    ref_mpf(1, &rp);
    CHECK_EQ(rp.wtskid, 2);
    CHECK_EQ(rp.fblkcnt, 0u);
}

// ---- 待ち行列と状態の整合 ----
TEST_CASE(priority_change_reorders_a_priority_queue_but_not_fifo) {
    for (ATR atr : {(ATR)TA_TPRI, (ATR)TA_TFIFO}) {
        fresh();
        mk(1, 1);
        mk(2, 2);
        mk(3, 3);
        mk_sem(1, 0, 3, atr);
        CHECK_EQ(wai_sem(1), E_WBLK); // 到着順は 1, 2, 3
        CHECK_EQ(wai_sem(1), E_WBLK);
        CHECK_EQ(wai_sem(1), E_WBLK);
        // 待ち中に、先頭のタスク 1 を最低、タスク 3 を最高の優先度にする
        CHECK_EQ(chg_pri(1, 5), E_OK);
        CHECK_EQ(chg_pri(3, 1), E_OK);
        const ID first = (atr == TA_TPRI) ? 3 : 1; // 優先度順なら 3、FIFO なら到着順の 1
        const ID second = (atr == TA_TPRI) ? 2 : 2;
        T_RSEM rs;
        ref_sem(1, &rs);
        CHECK_EQ(rs.wtskid, first);
        CHECK_EQ(sig_sem(1), E_OK);
        CHECK_EQ(uitron_wait_result(first), E_OK);
        CHECK_EQ(rt(second).tskstat, TTS_WAI);
        ref_sem(1, &rs);
        CHECK_EQ(rs.wtskid, second);
    }
}

TEST_CASE(forced_release_termination_and_deletion_keep_queues_consistent) {
    fresh();
    mk(1, 1);
    mk(2, 2);
    mk(3, 3);
    mk_sem(1, 0, 3);
    CHECK_EQ(wai_sem(1), E_WBLK);
    CHECK_EQ(wai_sem(1), E_WBLK);
    CHECK_EQ(wai_sem(1), E_WBLK);

    CHECK_EQ(rel_wai(1), E_OK); // 強制解除: E_RLWAI で待ち行列から外れる
    CHECK_EQ(uitron_wait_result(1), E_RLWAI);
    CHECK_EQ(rt(1).tskwait, 0u);
    T_RSEM rs;
    ref_sem(1, &rs);
    CHECK_EQ(rs.wtskid, 2);

    CHECK_EQ(ter_tsk(2), E_OK); // 終了したタスクは待ち行列にも資源にも残らない
    ref_sem(1, &rs);
    CHECK_EQ(rs.wtskid, 3);
    CHECK_EQ(rt(2).tskstat, TTS_DMT);
    CHECK_EQ(sig_sem(1), E_OK);
    CHECK_EQ(uitron_wait_result(3), E_OK);
    ref_sem(1, &rs);
    CHECK_EQ(rs.semcnt, 0u);

    // 削除で残りの待ちタスクは E_DLT
    mk_sem(2, 0, 1);
    CHECK_EQ(act_tsk(2), E_OK);
    CHECK_EQ(uitron_os_get_current_task(), 1);
    CHECK_EQ(wai_sem(2), E_WBLK);
    CHECK_EQ(del_sem(2), E_OK);
    CHECK_EQ(uitron_wait_result(1), E_DLT);
    CHECK_EQ(rt(1).tskstat != TTS_WAI, true);
}

TEST_CASE(double_wait_state_survives_object_operations) {
    fresh();
    mk(1, 1);
    mk_sem(1, 0, 2);
    CHECK_EQ(wai_sem(1), E_WBLK);
    CHECK_EQ(sus_tsk(1), E_OK); // 待ち + 強制待ち = 二重待ち
    CHECK_EQ(rt(1).tskstat, TTS_WAS);
    CHECK_EQ(rt(1).suscnt, 1u);
    CHECK_EQ(sus_tsk(1), E_OK);
    CHECK_EQ(rt(1).suscnt, 2u);
    T_RSEM rs;
    ref_sem(1, &rs);
    CHECK_EQ(rs.wtskid, 1); // 二重待ちも待ち行列にいる
    CHECK_EQ(sig_sem(1), E_OK); // 資源を渡すが、強制待ちは残る
    CHECK_EQ(rt(1).tskstat, TTS_SUS);
    CHECK_EQ(uitron_wait_result(1), E_OK);
    ref_sem(1, &rs);
    CHECK_EQ(rs.semcnt, 0u);
    CHECK_EQ(rsm_tsk(1), E_OK);
    CHECK_EQ(rt(1).tskstat, TTS_SUS); // ネストが残る
    CHECK_EQ(rsm_tsk(1), E_OK);
    CHECK_EQ(rt(1).tskstat != TTS_SUS, true);
}

TEST_CASE(double_wait_state_with_release_and_wakeup) {
    fresh();
    mk(1, 1);
    mk(2, 2);
    CHECK_EQ(slp_tsk(), E_WBLK); // 1
    CHECK_EQ(sus_tsk(1), E_OK);
    CHECK_EQ(rt(1).tskstat, TTS_WAS);
    CHECK_EQ(wup_tsk(1), E_OK); // 起床すると待ちは解けて強制待ちだけが残る
    CHECK_EQ(rt(1).tskstat, TTS_SUS);
    CHECK_EQ(rt(1).wupcnt, 0u);
    CHECK_EQ(uitron_wait_result(1), E_OK);
    CHECK_EQ(rsm_tsk(1), E_OK);
    CHECK_EQ(rt(1).tskstat != TTS_SUS, true);

    // rel_wai でも同じ
    CHECK_EQ(dly_tsk(100), E_WBLK);
    CHECK_EQ(sus_tsk(1), E_OK);
    CHECK_EQ(rel_wai(1), E_OK);
    CHECK_EQ(rt(1).tskstat, TTS_SUS);
    CHECK_EQ(uitron_wait_result(1), E_RLWAI);
    // 資源待ちの二重待ちが時間切れ
    mk_sem(1, 0, 1);
    CHECK_EQ(rsm_tsk(1), E_OK);
    CHECK_EQ(twai_sem(1, 2), E_WBLK);
    CHECK_EQ(sus_tsk(1), E_OK);
    uitron_os_tick(2);
    CHECK_EQ(rt(1).tskstat, TTS_SUS);
    CHECK_EQ(uitron_wait_result(1), E_TMOUT);
}

TEST_CASE(suspend_and_resume_nesting) {
    fresh();
    mk(1, 1);
    mk(2, 2);
    CHECK_EQ(sus_tsk(1), E_OK); // 実行中のタスクの強制待ち
    CHECK_EQ(rt(1).tskstat, TTS_SUS);
    CHECK_EQ(uitron_os_get_current_task(), 2);
    for (int i = 0; i < 254; ++i) CHECK_EQ(sus_tsk(1), E_OK);
    CHECK_EQ(rt(1).suscnt, 255u);
    CHECK_EQ(sus_tsk(1), E_QOVR);
    for (int i = 0; i < 254; ++i) CHECK_EQ(rsm_tsk(1), E_OK);
    CHECK_EQ(rt(1).tskstat, TTS_SUS);
    CHECK_EQ(rsm_tsk(1), E_OK);
    CHECK_EQ(rt(1).tskstat, TTS_RUN); // 最高優先度に戻って実行状態
    CHECK_EQ(uitron_os_get_current_task(), 1);
}

TEST_CASE(activation_queueing_and_priority_changes) {
    fresh();
    mk(1, 3);
    CHECK_EQ(act_tsk(1), E_OK); // 休止でないので要求がキューに入る
    CHECK_EQ(act_tsk(1), E_OK);
    CHECK_EQ(iact_tsk(1), E_OK);
    CHECK_EQ(rt(1).actcnt, 3u);
    CHECK_EQ(can_act(1), 3);
    CHECK_EQ(can_act(1), 0);
    for (int i = 0; i < 255; ++i) CHECK_EQ(act_tsk(1), E_OK);
    CHECK_EQ(act_tsk(1), E_QOVR);

    CHECK_EQ(chg_pri(1, 7), E_OK);
    CHECK_EQ(rt(1).tskpri, 7);
    CHECK_EQ(rt(1).tskbpri, 3); // ベース優先度は変わらない
    CHECK_EQ(chg_pri(1, 0), E_OK); // 0 は初期優先度へ戻す
    CHECK_EQ(rt(1).tskpri, 3);
    CHECK_EQ(chg_pri(1, UITRON_MAX_PRI + 1), E_PAR);
    CHECK_EQ(chg_pri(1, -1), E_PAR);
    CHECK_EQ(chg_pri(1, UITRON_MAX_PRI), E_OK);
    PRI p = 0;
    CHECK_EQ(get_pri(1, &p), E_OK);
    CHECK_EQ(p, UITRON_MAX_PRI);
}

TEST_CASE(task_self_resolves_to_the_running_task) {
    fresh();
    mk(1, 2);
    mk(2, 1); // こちらが先に実行状態になる
    CHECK_EQ(uitron_os_get_current_task(), 2);
    PRI p = 0;
    CHECK_EQ(get_pri(TSK_SELF, &p), E_OK);
    CHECK_EQ(p, 1);
    CHECK_EQ(chg_pri(TSK_SELF, 5), E_OK);
    CHECK_EQ(rt(2).tskpri, 5);
    CHECK_EQ(uitron_os_get_current_task(), 1); // 優先度が下がって交代
    CHECK_EQ(get_pri(TSK_SELF, &p), E_OK);
    CHECK_EQ(p, 2);
}

// ---- ディスパッチ・CPU ロック・時刻 ----
TEST_CASE(dispatch_and_cpu_lock_defer_scheduling) {
    fresh();
    mk(1, 5);
    CHECK_EQ(uitron_os_get_current_task(), 1);
    CHECK(!sns_loc());
    CHECK(!sns_dsp());
    CHECK(!sns_dpn());
    CHECK(!sns_ctx());

    CHECK_EQ(dis_dsp(), E_OK);
    CHECK(sns_dsp());
    CHECK(sns_dpn());
    mk(2, 1); // 高優先度でもディスパッチ禁止中は切り替わらない
    CHECK_EQ(uitron_os_get_current_task(), 1);
    CHECK_EQ(ena_dsp(), E_OK);
    CHECK(!sns_dsp());
    CHECK_EQ(uitron_os_get_current_task(), 2);

    CHECK_EQ(loc_cpu(), E_OK);
    CHECK(sns_loc());
    CHECK(sns_dpn());
    CHECK(!sns_dsp());
    CHECK_EQ(chg_pri(2, 9), E_OK);
    CHECK_EQ(uitron_os_get_current_task(), 2); // 切り替わらない
    CHECK_EQ(unl_cpu(), E_OK);
    CHECK(!sns_loc());
    CHECK_EQ(uitron_os_get_current_task(), 1);

    CHECK_EQ(dis_dsp(), E_OK);
    CHECK_EQ(loc_cpu(), E_OK);
    CHECK_EQ(unl_cpu(), E_OK);
    CHECK(sns_dpn()); // ディスパッチ禁止は残る
    CHECK_EQ(ena_dsp(), E_OK);
    CHECK(!sns_dpn());
}

TEST_CASE(system_time) {
    fresh();
    SYSTIM t = 99;
    CHECK_EQ(get_tim(&t), E_OK);
    CHECK_EQ(t, (SYSTIM)0);
    SYSTIM set = 123456789012ULL;
    CHECK_EQ(set_tim(&set), E_OK);
    CHECK_EQ(isig_tim(), E_OK);
    uitron_os_tick(10);
    get_tim(&t);
    CHECK_EQ(t, (SYSTIM)123456789023ULL);
    uitron_os_shutdown(); // シャットダウンで初期状態に戻る
    get_tim(&t);
    CHECK_EQ(t, (SYSTIM)0);
    CHECK_EQ(del_tsk(1), E_NOEXS);
}

TEST_CASE(terminate_and_delete_reset_task_state) {
    fresh();
    mk(1, 2);
    mk_sem(1, 0, 1);
    CHECK_EQ(twai_sem(1, 10), E_WBLK);
    CHECK_EQ(wup_tsk(1), E_OK);       // 待ちの種類が違うので起床要求がたまる
    CHECK_EQ(sus_tsk(1), E_OK);
    CHECK_EQ(ter_tsk(1), E_OK);
    T_RTSK r = rt(1);
    CHECK_EQ(r.tskstat, TTS_DMT);
    CHECK_EQ(r.tskwait, 0u);
    CHECK_EQ(r.wid, 0);
    CHECK_EQ(r.lefttmo, 0u);
    CHECK_EQ(r.wupcnt, 0u);
    CHECK_EQ(r.suscnt, 0u);
    CHECK_EQ(r.tskpri, r.tskbpri);
    CHECK_EQ(uitron_wait_result(1), E_OK);

    CHECK_EQ(chg_pri(1, 7), E_OBJ);
    CHECK_EQ(act_tsk(1), E_OK); // 休止状態からの起動
    CHECK_EQ(rt(1).tskpri, 2); // ベース優先度で再開
    CHECK_EQ(rt(1).tskstat, TTS_RUN);
    CHECK_EQ(ter_tsk(1), E_OK);
    CHECK_EQ(del_tsk(1), E_OK);
    CHECK_EQ(act_tsk(1), E_NOEXS);
    mk(1, 4, TA_HLNG); // 同じ ID を作り直せる
    CHECK_EQ(rt(1).tskbpri, 4);
}

EMBED_TEST_MAIN()
