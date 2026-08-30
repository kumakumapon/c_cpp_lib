#include <iostream>
#include <cstdio>
#include "uitron/itron.h"
#include "uitron/kernel.h"
#include "uitron/uitron_os.h"

#define TASK_SENSOR_ID  1
#define TASK_LOGGER_ID  2
#define SEM_RESOURCE_ID 1
#define FLG_EVENT_ID    1
#define MBX_DATA_ID     1
#define MPF_BLOCK_ID    1

// センサーデータ構造体
typedef struct {
    T_MSG head;
    int sensor_id;
    float temperature;
    float humidity;
} SensorPacket;

static uint8_t s_mpf_pool[sizeof(SensorPacket) * 4];

static void print_step(const char *msg) {
    std::cout << "\n>>> " << msg << " <<<\n";
}

int main() {
    std::cout << "=====================================================" << std::endl;
    std::cout << " μITRON 4.0 RTOS API Library Demonstration" << std::endl;
    std::cout << "=====================================================" << std::endl;

    // 1. OS初期化
    uitron_os_init();

    // 2. セマフォ生成 (共有リソース排他用: 初期値1, 最大1)
    T_CSEM csem{};
    csem.sematr = TA_TFIFO;
    csem.isemcnt = 1;
    csem.maxsem = 1;
    cre_sem(SEM_RESOURCE_ID, &csem);

    // 3. イベントフラグ生成 (通知同期用: 初期値0)
    T_CFLG cflg{};
    cflg.flgatr = TA_WMUL | TA_CLR;
    cflg.iflgptn = 0x00;
    cre_flg(FLG_EVENT_ID, &cflg);

    // 4. メールボックス生成 (パケット受送信用)
    T_CMBX cmbx{};
    cmbx.mbxatr = TA_TFIFO | TA_MFIFO;
    cre_mbx(MBX_DATA_ID, &cmbx);

    // 5. 固定長メモリプール生成 (静的バッファ割り当て: 4ブロック)
    T_CMPF cmpf{};
    cmpf.mpfatr = TA_TFIFO;
    cmpf.blkcnt = 4;
    cmpf.blksz = sizeof(SensorPacket);
    cmpf.mpf = s_mpf_pool;
    cre_mpf(MPF_BLOCK_ID, &cmpf);

    print_step("Step 1: Producer - Allocating packet from MPF and sending to Mailbox");
    SensorPacket *pkt = nullptr;
    ER er = pget_mpf(MPF_BLOCK_ID, (VP *)&pkt);
    if (er == E_OK && pkt) {
        pkt->sensor_id = 101;
        pkt->temperature = 26.8f;
        pkt->humidity = 62.4f;
        std::printf("  [Producer] Created packet: ID=%d, Temp=%.1f C, Humid=%.1f %%\n",
                    pkt->sensor_id, pkt->temperature, pkt->humidity);

        // メールボックスへ送信
        snd_mbx(MBX_DATA_ID, &pkt->head);
        std::printf("  [Producer] Sent packet to Mailbox (ID=%d)\n", MBX_DATA_ID);

        // イベントフラグセット (0x01: データ受信準備完了)
        set_flg(FLG_EVENT_ID, 0x01);
        std::printf("  [Producer] Set event flag: 0x01\n");
    }

    print_step("Step 2: Consumer - Checking event flag and receiving packet");
    UINT flgptn = 0;
    er = pol_flg(FLG_EVENT_ID, 0x01, TWF_ANDW | TWF_CLR, &flgptn);
    if (er == E_OK) {
        std::printf("  [Consumer] Event flag matched (Pattern: 0x%02X)\n", flgptn);

        T_MSG *rcv_head = nullptr;
        er = prcv_mbx(MBX_DATA_ID, &rcv_head);
        if (er == E_OK && rcv_head) {
            SensorPacket *recv_pkt = (SensorPacket *)rcv_head;
            std::printf("  [Consumer] Received packet: ID=%d, Temp=%.1f C, Humid=%.1f %%\n",
                        recv_pkt->sensor_id, recv_pkt->temperature, recv_pkt->humidity);

            // 排他リソース(セマフォ)を使ってログ出力
            wai_sem(SEM_RESOURCE_ID);
            std::printf("  [Logger] [LOCKED] Log entry saved successfully.\n");
            sig_sem(SEM_RESOURCE_ID);

            // メモリプールへ返却
            rel_mpf(MPF_BLOCK_ID, recv_pkt);
            std::printf("  [Consumer] Returned memory block to MPF.\n");
        }
    }

    print_step("Step 3: Verification of resource counts");
    T_RMPF rmpf{};
    ref_mpf(MPF_BLOCK_ID, &rmpf);
    std::printf("  [Status] Free MPF blocks remaining: %u / 4\n", rmpf.fblkcnt);

    T_RSEM rsem{};
    ref_sem(SEM_RESOURCE_ID, &rsem);
    std::printf("  [Status] Semaphore count: %u / 1\n", rsem.semcnt);

    std::cout << "\n=====================================================" << std::endl;
    std::cout << " μITRON 4.0 Demonstration completed successfully!" << std::endl;
    std::cout << "=====================================================" << std::endl;
    return 0;
}
