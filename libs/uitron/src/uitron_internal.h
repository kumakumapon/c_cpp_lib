#pragma once
#ifndef UITRON_INTERNAL_H
#define UITRON_INTERNAL_H

#include "uitron/itron.h"
#include "uitron/kernel.h"
#include "uitron/uitron_os.h"

#ifdef __cplusplus
extern "C" {
#endif

/* タスクコントロールブロック (TCB) */
typedef struct tcb {
    ID      tskid;
    BOOL    is_created;
    T_CTSK  ctsk;
    STAT    tskstat;        /* TTS_RUN, TTS_RDY, TTS_WAI, TTS_SUS, TTS_DMT */
    PRI     tskpri;         /* 現在の優先度 */
    PRI     tskbpri;        /* ベース優先度 */
    STAT    tskwait;        /* 待ち要因 (TTW_*) */
    ID      wid;            /* 待ち先オブジェクトID */
    TMO     lefttmo;        /* 残りタイムアウト時間 */
    UINT    actcnt;         /* 起動要求キューイング数 */
    UINT    wupcnt;         /* 起床要求キューイング数 */
    UINT    suscnt;         /* サスペンドネスト数 */
    
    /* イベントフラグ待ち用 */
    UINT    waiptn;
    MODE    wfmode;
    UINT    *p_flgptn;

    /* メールボックス待ち用 */
    T_MSG   **ppk_msg;

    /* メモリプール待ち用 */
    VP      *p_mpf_blk;

    ER      wait_er;        /* 待ち解除時のエラーコード */
    uint32_t wait_seq;      /* 待ちに入った順序 (TA_TFIFO の待ち行列順序) */
} TCB;

/* セマフォコントロールブロック (SCB) */
typedef struct scb {
    ID      semid;
    BOOL    is_created;
    T_CSEM  csem;
    UINT    semcnt;
} SCB;

/* イベントフラグコントロールブロック (FCB) */
typedef struct fcb {
    ID      flgid;
    BOOL    is_created;
    T_CFLG  cflg;
    UINT    flgptn;
} FCB;

/* メールボックスコントロールブロック (MCB) */
typedef struct mcb {
    ID      mbxid;
    BOOL    is_created;
    T_CMBX  cmbx;
    T_MSG   *head;
    T_MSG   *tail;
} MCB;

/* 固定長メモリプールコントロールブロック (PCB) */
typedef struct pcb {
    ID      mpfid;
    BOOL    is_created;
    T_CMPF  cmpf;
    uint8_t *pool_mem;
    BOOL    is_allocated_internally;
    uint8_t *free_list;     /* 空きブロックリンクリスト */
    UINT    fblkcnt;
} PCB;

/* グローバルカーネル状態 */
typedef struct {
    TCB     tcbs[UITRON_MAX_TSKID + 1];
    SCB     scbs[UITRON_MAX_SEMID + 1];
    FCB     fcbs[UITRON_MAX_FLGID + 1];
    MCB     mcbs[UITRON_MAX_MBXID + 1];
    PCB     pcbs[UITRON_MAX_MPFID + 1];

    ID      cur_tskid;      /* スケジューラが選んだ実行状態のタスクID */
    ID      exec_tskid;     /* エントリ関数を実行中のタスクID (ステップ実行中のみ有効) */
    uint32_t wait_seq_counter; /* 待ち順序の採番 */
    BOOL    dsp_enabled;    /* ディスパッチ許可フラグ */
    BOOL    cpu_locked;     /* CPUロックフラグ */
    SYSTIM  systim;         /* システム時刻 (ms) */
} KERNEL_CB;

extern KERNEL_CB g_kcb;

/* 内部ヘルパー関数 */
TCB *uitron_get_tcb(ID tskid);
void uitron_task_ready(TCB *tcb);
void uitron_task_wait(TCB *tcb, STAT tskwait, ID wid, TMO tmout);
void uitron_task_wakeup(TCB *tcb, ER er);
void uitron_reschedule(void);
ID uitron_caller_tskid(void);
TCB *uitron_pick_waiter(STAT tskwait, ID wid, BOOL by_priority);

#ifdef __cplusplus
}
#endif

#endif /* UITRON_INTERNAL_H */
