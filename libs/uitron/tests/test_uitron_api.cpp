#include "embed_test/embed_test.hpp"
#include <cstring>
#include <iostream>

#include "uitron/itron.h"
#include "uitron/kernel.h"
#include "uitron/uitron_os.h"

// このファイルは各サービスコールの戻り値・状態遷移を単体で検証する。タスクの本体は実行しない
// (uitron_os_run_step() を呼ばない)ので、エントリ関数は呼ばれないことを最後に確認する。
// 複数タスクが協調動作する検証は test_uitron_scenarios.cpp にある。
static int g_entry_calls = 0;
static void never_run_entry(void) { g_entry_calls++; }

TEST_CASE(test_task_management) {
    std::cout << "# [Test] Running task management tests..." << std::endl;
    uitron_os_init();

    T_CTSK ctsk{};
    ctsk.tskatr = TA_HLNG;
    ctsk.itskpri = 5;
    ctsk.task = never_run_entry;
    ctsk.stksz = 1024;

    ER er = cre_tsk(1, &ctsk);
    CHECK(er == E_OK);

    T_RTSK rtsk{};
    er = ref_tsk(1, &rtsk);
    CHECK(er == E_OK);
    CHECK(rtsk.tskstat == TTS_DMT);
    CHECK(rtsk.tskpri == 5);

    // 起動
    er = act_tsk(1);
    CHECK(er == E_OK);

    er = ref_tsk(1, &rtsk);
    CHECK(rtsk.tskstat == TTS_RUN || rtsk.tskstat == TTS_RDY);

    // 優先度変更
    er = chg_pri(1, 2);
    CHECK(er == E_OK);
    PRI pri = 0;
    er = get_pri(1, &pri);
    CHECK(er == E_OK && pri == 2);

    // 終了
    er = ter_tsk(1);
    CHECK(er == E_OK);

    er = del_tsk(1);
    CHECK(er == E_OK);

    std::cout << "# [Test] Task management passed!" << std::endl;
}

TEST_CASE(test_semaphore) {
    std::cout << "# [Test] Running semaphore tests..." << std::endl;
    uitron_os_init();

    T_CSEM csem{};
    csem.sematr = TA_TFIFO;
    csem.isemcnt = 1;
    csem.maxsem = 3;

    ER er = cre_sem(1, &csem);
    CHECK(er == E_OK);

    // ポーリング獲得
    er = pol_sem(1);
    CHECK(er == E_OK);

    // 資源枯渇でポーリング失敗
    er = pol_sem(1);
    CHECK(er == E_TMOUT);

    // 返却
    er = sig_sem(1);
    CHECK(er == E_OK);
    er = sig_sem(1);
    CHECK(er == E_OK);

    T_RSEM rsem{};
    er = ref_sem(1, &rsem);
    CHECK(er == E_OK);
    CHECK(rsem.semcnt == 2);

    er = del_sem(1);
    CHECK(er == E_OK);

    std::cout << "# [Test] Semaphore passed!" << std::endl;
}

TEST_CASE(test_event_flag) {
    std::cout << "# [Test] Running event flag tests..." << std::endl;
    uitron_os_init();

    T_CFLG cflg{};
    cflg.flgatr = TA_WMUL;
    cflg.iflgptn = 0x01;

    ER er = cre_flg(1, &cflg);
    CHECK(er == E_OK);

    UINT ptn = 0;
    // OR待ち (0x01 OR 0x02) -> 0x01 が立っているので成立
    er = pol_flg(1, 0x03, TWF_ORW, &ptn);
    CHECK(er == E_OK);
    CHECK(ptn == 0x01);

    // AND待ち (0x01 AND 0x02) -> 0x02 が立っていないので不成立
    er = pol_flg(1, 0x03, TWF_ANDW, &ptn);
    CHECK(er == E_TMOUT);

    // フラグセット
    er = set_flg(1, 0x02);
    CHECK(er == E_OK);

    // AND待ち (TWF_CLR指定) -> 成立してクリアされる
    er = pol_flg(1, 0x03, TWF_ANDW | TWF_CLR, &ptn);
    CHECK(er == E_OK);
    CHECK((ptn & 0x03) == 0x03);

    T_RFLG rflg{};
    er = ref_flg(1, &rflg);
    CHECK(er == E_OK);
    CHECK(rflg.flgptn == 0); // クリアされていること

    er = del_flg(1);
    CHECK(er == E_OK);

    std::cout << "# [Test] Event flag passed!" << std::endl;
}

TEST_CASE(test_mailbox) {
    std::cout << "# [Test] Running mailbox tests..." << std::endl;
    uitron_os_init();

    T_CMBX cmbx{};
    cmbx.mbxatr = TA_TFIFO | TA_MFIFO;

    ER er = cre_mbx(1, &cmbx);
    CHECK(er == E_OK);

    struct CustomMsg {
        T_MSG head;
        int data;
    } msg1, msg2;

    msg1.data = 100;
    msg2.data = 200;

    er = snd_mbx(1, &msg1.head);
    CHECK(er == E_OK);
    er = snd_mbx(1, &msg2.head);
    CHECK(er == E_OK);

    T_MSG *rcv_head = nullptr;
    er = prcv_mbx(1, &rcv_head);
    CHECK(er == E_OK);
    CustomMsg *m1 = (CustomMsg *)rcv_head;
    CHECK(m1->data == 100);

    er = prcv_mbx(1, &rcv_head);
    CHECK(er == E_OK);
    CustomMsg *m2 = (CustomMsg *)rcv_head;
    CHECK(m2->data == 200);

    // 空の受信
    er = prcv_mbx(1, &rcv_head);
    CHECK(er == E_TMOUT);

    er = del_mbx(1);
    CHECK(er == E_OK);

    std::cout << "# [Test] Mailbox passed!" << std::endl;
}

TEST_CASE(test_memory_pool) {
    std::cout << "# [Test] Running memory pool tests..." << std::endl;
    uitron_os_init();

    static uint8_t s_pool_buf[512];
    T_CMPF cmpf{};
    cmpf.mpfatr = TA_TFIFO;
    cmpf.blkcnt = 4;
    cmpf.blksz = 64;
    cmpf.mpf = s_pool_buf;

    ER er = cre_mpf(1, &cmpf);
    CHECK(er == E_OK);

    VP blk1 = nullptr;
    VP blk2 = nullptr;
    VP blk3 = nullptr;
    VP blk4 = nullptr;
    VP blk5 = nullptr;

    er = pget_mpf(1, &blk1);
    CHECK(er == E_OK && blk1 != nullptr);
    er = pget_mpf(1, &blk2);
    CHECK(er == E_OK && blk2 != nullptr);
    er = pget_mpf(1, &blk3);
    CHECK(er == E_OK && blk3 != nullptr);
    er = pget_mpf(1, &blk4);
    CHECK(er == E_OK && blk4 != nullptr);

    // 枯渇
    er = pget_mpf(1, &blk5);
    CHECK(er == E_TMOUT);

    // 返却
    er = rel_mpf(1, blk2);
    CHECK(er == E_OK);

    // 再獲得
    er = pget_mpf(1, &blk5);
    CHECK(er == E_OK && blk5 == blk2);

    rel_mpf(1, blk1);
    rel_mpf(1, blk3);
    rel_mpf(1, blk4);
    rel_mpf(1, blk5);

    T_RMPF rmpf{};
    ref_mpf(1, &rmpf);
    CHECK(rmpf.fblkcnt == 4);

    del_mpf(1);
    std::cout << "# [Test] Memory pool passed!" << std::endl;
}


// ---- 起動要求キュー ---------------------------------------------------------
TEST_CASE(test_terminate_restarts_when_an_activation_is_queued) {
    uitron_os_init();
    T_CTSK c{};
    c.tskatr = TA_HLNG | TA_ACT;
    c.itskpri = 3;
    c.task = never_run_entry;
    c.stksz = 64;
    REQUIRE(cre_tsk(1, &c) == E_OK);

    T_RTSK rt{};
    CHECK(act_tsk(1) == E_OK); // 実行中への起動要求はキューイングされる
    REQUIRE(ref_tsk(1, &rt) == E_OK);
    CHECK(rt.actcnt == 1);

    CHECK(ter_tsk(1) == E_OK); // 強制終了すると、キュー済みの1件で再起動する
    REQUIRE(ref_tsk(1, &rt) == E_OK);
    CHECK(rt.actcnt == 0);
    CHECK(rt.tskstat == TTS_RUN || rt.tskstat == TTS_RDY);

    CHECK(ter_tsk(1) == E_OK); // キューが空なら休止状態になる
    REQUIRE(ref_tsk(1, &rt) == E_OK);
    CHECK(rt.tskstat == TTS_DMT);
    CHECK(ter_tsk(1) == E_OBJ); // 休止中の終了要求は状態不正
}

TEST_CASE(test_api_tests_never_executed_a_task_body) { CHECK(g_entry_calls == 0); }

EMBED_TEST_MAIN()
