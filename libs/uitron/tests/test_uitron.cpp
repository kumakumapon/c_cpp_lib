#include <iostream>
#include <cassert>
#include <cstring>

#include "uitron/itron.h"
#include "uitron/kernel.h"
#include "uitron/uitron_os.h"

static int g_task1_counter = 0;
static void task1_entry(void) {
    g_task1_counter++;
}

void test_task_management() {
    std::cout << "[Test] Running task management tests..." << std::endl;
    uitron_os_init();

    T_CTSK ctsk{};
    ctsk.tskatr = TA_HLNG;
    ctsk.itskpri = 5;
    ctsk.task = task1_entry;
    ctsk.stksz = 1024;

    ER er = cre_tsk(1, &ctsk);
    assert(er == E_OK);

    T_RTSK rtsk{};
    er = ref_tsk(1, &rtsk);
    assert(er == E_OK);
    assert(rtsk.tskstat == TTS_DMT);
    assert(rtsk.tskpri == 5);

    // 起動
    er = act_tsk(1);
    assert(er == E_OK);

    er = ref_tsk(1, &rtsk);
    assert(rtsk.tskstat == TTS_RUN || rtsk.tskstat == TTS_RDY);

    // 優先度変更
    er = chg_pri(1, 2);
    assert(er == E_OK);
    PRI pri = 0;
    er = get_pri(1, &pri);
    assert(er == E_OK && pri == 2);

    // 終了
    er = ter_tsk(1);
    assert(er == E_OK);

    er = del_tsk(1);
    assert(er == E_OK);

    std::cout << "[Test] Task management passed!" << std::endl;
}

void test_semaphore() {
    std::cout << "[Test] Running semaphore tests..." << std::endl;
    uitron_os_init();

    T_CSEM csem{};
    csem.sematr = TA_TFIFO;
    csem.isemcnt = 1;
    csem.maxsem = 3;

    ER er = cre_sem(1, &csem);
    assert(er == E_OK);

    // ポーリング獲得
    er = pol_sem(1);
    assert(er == E_OK);

    // 資源枯渇でポーリング失敗
    er = pol_sem(1);
    assert(er == E_TMOUT);

    // 返却
    er = sig_sem(1);
    assert(er == E_OK);
    er = sig_sem(1);
    assert(er == E_OK);

    T_RSEM rsem{};
    er = ref_sem(1, &rsem);
    assert(er == E_OK);
    assert(rsem.semcnt == 2);

    er = del_sem(1);
    assert(er == E_OK);

    std::cout << "[Test] Semaphore passed!" << std::endl;
}

void test_event_flag() {
    std::cout << "[Test] Running event flag tests..." << std::endl;
    uitron_os_init();

    T_CFLG cflg{};
    cflg.flgatr = TA_WMUL;
    cflg.iflgptn = 0x01;

    ER er = cre_flg(1, &cflg);
    assert(er == E_OK);

    UINT ptn = 0;
    // OR待ち (0x01 OR 0x02) -> 0x01 が立っているので成立
    er = pol_flg(1, 0x03, TWF_ORW, &ptn);
    assert(er == E_OK);
    assert(ptn == 0x01);

    // AND待ち (0x01 AND 0x02) -> 0x02 が立っていないので不成立
    er = pol_flg(1, 0x03, TWF_ANDW, &ptn);
    assert(er == E_TMOUT);

    // フラグセット
    er = set_flg(1, 0x02);
    assert(er == E_OK);

    // AND待ち (TWF_CLR指定) -> 成立してクリアされる
    er = pol_flg(1, 0x03, TWF_ANDW | TWF_CLR, &ptn);
    assert(er == E_OK);
    assert((ptn & 0x03) == 0x03);

    T_RFLG rflg{};
    er = ref_flg(1, &rflg);
    assert(er == E_OK);
    assert(rflg.flgptn == 0); // クリアされていること

    er = del_flg(1);
    assert(er == E_OK);

    std::cout << "[Test] Event flag passed!" << std::endl;
}

void test_mailbox() {
    std::cout << "[Test] Running mailbox tests..." << std::endl;
    uitron_os_init();

    T_CMBX cmbx{};
    cmbx.mbxatr = TA_TFIFO | TA_MFIFO;

    ER er = cre_mbx(1, &cmbx);
    assert(er == E_OK);

    struct CustomMsg {
        T_MSG head;
        int data;
    } msg1, msg2;

    msg1.data = 100;
    msg2.data = 200;

    er = snd_mbx(1, &msg1.head);
    assert(er == E_OK);
    er = snd_mbx(1, &msg2.head);
    assert(er == E_OK);

    T_MSG *rcv_head = nullptr;
    er = prcv_mbx(1, &rcv_head);
    assert(er == E_OK);
    CustomMsg *m1 = (CustomMsg *)rcv_head;
    assert(m1->data == 100);

    er = prcv_mbx(1, &rcv_head);
    assert(er == E_OK);
    CustomMsg *m2 = (CustomMsg *)rcv_head;
    assert(m2->data == 200);

    // 空の受信
    er = prcv_mbx(1, &rcv_head);
    assert(er == E_TMOUT);

    er = del_mbx(1);
    assert(er == E_OK);

    std::cout << "[Test] Mailbox passed!" << std::endl;
}

void test_memory_pool() {
    std::cout << "[Test] Running memory pool tests..." << std::endl;
    uitron_os_init();

    static uint8_t s_pool_buf[512];
    T_CMPF cmpf{};
    cmpf.mpfatr = TA_TFIFO;
    cmpf.blkcnt = 4;
    cmpf.blksz = 64;
    cmpf.mpf = s_pool_buf;

    ER er = cre_mpf(1, &cmpf);
    assert(er == E_OK);

    VP blk1 = nullptr;
    VP blk2 = nullptr;
    VP blk3 = nullptr;
    VP blk4 = nullptr;
    VP blk5 = nullptr;

    er = pget_mpf(1, &blk1);
    assert(er == E_OK && blk1 != nullptr);
    er = pget_mpf(1, &blk2);
    assert(er == E_OK && blk2 != nullptr);
    er = pget_mpf(1, &blk3);
    assert(er == E_OK && blk3 != nullptr);
    er = pget_mpf(1, &blk4);
    assert(er == E_OK && blk4 != nullptr);

    // 枯渇
    er = pget_mpf(1, &blk5);
    assert(er == E_TMOUT);

    // 返却
    er = rel_mpf(1, blk2);
    assert(er == E_OK);

    // 再獲得
    er = pget_mpf(1, &blk5);
    assert(er == E_OK && blk5 == blk2);

    rel_mpf(1, blk1);
    rel_mpf(1, blk3);
    rel_mpf(1, blk4);
    rel_mpf(1, blk5);

    T_RMPF rmpf{};
    ref_mpf(1, &rmpf);
    assert(rmpf.fblkcnt == 4);

    del_mpf(1);
    std::cout << "[Test] Memory pool passed!" << std::endl;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << " Running μITRON 4.0 API test suite..." << std::endl;
    std::cout << "========================================" << std::endl;

    test_task_management();
    test_semaphore();
    test_event_flag();
    test_mailbox();
    test_memory_pool();

    std::cout << "========================================" << std::endl;
    std::cout << " All μITRON tests PASSED successfully!" << std::endl;
    std::cout << "========================================" << std::endl;
    return 0;
}
