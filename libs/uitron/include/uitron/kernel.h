#pragma once
#ifndef KERNEL_H
#define KERNEL_H

#include "itron.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------
 * オブジェクト属性 (ATR)
 * --------------------------------------------------------- */
#define TA_NULL     0x00U       /**< デフォルト属性 */
#define TA_HLNG     0x00U       /**< 高級言語用 */
#define TA_ASM      0x01U       /**< アセンブリ言語用 */
#define TA_ACT      0x02U       /**< 生成時起動タスク */
#define TA_TPRI     0x01U       /**< タスクの待ち行列を優先度順にする */
#define TA_TFIFO    0x00U       /**< タスクの待ち行列をFIFO順にする */
#define TA_MPRI     0x02U       /**< メッセージ優先度順 */
#define TA_MFIFO    0x00U       /**< メッセージFIFO順 */
#define TA_WSGL     0x00U       /**< 単一タスク待ち */
#define TA_WMUL     0x08U       /**< 複数タスク待ち */
#define TA_CLR      0x04U       /**< イベントフラグ自動クリア */

/* イベントフラグ待ちモード (MODE) */
#define TWF_ANDW    0x00U       /**< AND待ち */
#define TWF_ORW     0x01U       /**< OR待ち */
#define TWF_CLR     0x02U       /**< 条件成立時にフラグクリア */

/* タスク状態 (STAT) */
#define TTS_RUN     0x01U       /**< 実行状態 (RUNNING) */
#define TTS_RDY     0x02U       /**< 実行可能状態 (READY) */
#define TTS_WAI     0x04U       /**< 待ち状態 (WAITING) */
#define TTS_SUS     0x08U       /**< 強制待ち状態 (SUSPENDED) */
#define TTS_WAS     0x0CU       /**< 二重待ち状態 (WAITING-SUSPENDED) */
#define TTS_DMT     0x10U       /**< 休止状態 (DORMANT) */

/* タスク待ち要因 (STAT) */
#define TTW_SLP     0x0001U     /**< 起床待ち (slp_tsk) */
#define TTW_DLY     0x0002U     /**< 時間経過待ち (dly_tsk) */
#define TTW_SEM     0x0004U     /**< セマフォ資源獲得待ち (wai_sem) */
#define TTW_FLG     0x0008U     /**< イベントフラグ待ち (wai_flg) */
#define TTW_MBX     0x0040U     /**< メールボックス受信待ち (rcv_mbx) */
#define TTW_MPF     0x2000U     /**< 固定長メモリブロック獲得待ち (get_mpf) */

/* ---------------------------------------------------------
 * パケット構造体定義
 * --------------------------------------------------------- */

/* タスク生成パケット */
typedef struct t_ctsk {
    ATR     tskatr;     /**< タスク属性 */
    VP_INT  exinf;      /**< 拡張情報 */
    FP      task;       /**< タスク起動番地 */
    PRI     itskpri;    /**< タスク起動時優先度 */
    size_t  stksz;      /**< スタックサイズ (バイト) */
    VP      stk;        /**< スタック領域の先頭番地 (NULL時は自動割当) */
} T_CTSK;

/* タスク状態参照パケット */
typedef struct t_rtsk {
    STAT    tskstat;    /**< タスク状態 */
    PRI     tskpri;     /**< 現在の優先度 */
    PRI     tskbpri;    /**< ベース優先度 */
    STAT    tskwait;    /**< 待ち要因 */
    ID      wid;        /**< 待ちオブジェクトID */
    TMO     lefttmo;    /**< 残り待ち時間 (ms) */
    UINT    actcnt;     /**< 起動要求キューイング数 */
    UINT    wupcnt;     /**< 起床要求キューイング数 */
    UINT    suscnt;     /**< サスペンド要求ネスト数 */
} T_RTSK;

/* セマフォ生成パケット */
typedef struct t_csem {
    ATR     sematr;     /**< セマフォ属性 */
    UINT    isemcnt;    /**< 初期セマフォ資源数 */
    UINT    maxsem;     /**< 最大セマフォ資源数 */
} T_CSEM;

/* セマフォ状態参照パケット */
typedef struct t_rsem {
    ID      wtskid;     /**< 待ち行列先頭のタスクID */
    UINT    semcnt;     /**< 現在のセマフォ資源数 */
} T_RSEM;

/* イベントフラグ生成パケット */
typedef struct t_cflg {
    ATR     flgatr;     /**< イベントフラグ属性 */
    UINT    iflgptn;    /**< 初期ビットパターン */
} T_CFLG;

/* イベントフラグ状態参照パケット */
typedef struct t_rflg {
    ID      wtskid;     /**< 待ち行列先頭のタスクID */
    UINT    flgptn;     /**< 現在のビットパターン */
} T_RFLG;

/* メールボックス メッセージヘッダ */
typedef struct t_msg {
    struct t_msg *next; /**< 次のメッセージへのポインタ */
} T_MSG;

typedef struct t_msg_pri {
    T_MSG   msghead;    /**< メッセージヘッダ */
    PRI     msgpri;     /**< メッセージ優先度 */
} T_MSG_PRI;

/* メールボックス生成パケット */
typedef struct t_cmbx {
    ATR     mbxatr;     /**< メールボックス属性 */
    PRI     maxmpri;    /**< 最大送信メッセージ優先度 */
    VP      mprihd;     /**< メッセージ優先度ヘッダ領域 */
} T_CMBX;

/* メールボックス状態参照パケット */
typedef struct t_rmbx {
    ID      wtskid;     /**< 受信待ちタスクID */
    T_MSG   *pk_msg;    /**< 先頭メッセージへのポインタ */
} T_RMBX;

/* 固定長メモリプール生成パケット */
typedef struct t_cmpf {
    ATR     mpfatr;     /**< メモリプール属性 */
    UINT    blkcnt;     /**< メモリブロック総数 */
    UINT    blksz;      /**< 1ブロックのサイズ (バイト) */
    VP      mpf;        /**< メモリプール領域の先頭番地 (NULL時は自動割当) */
} T_CMPF;

/* 固定長メモリプール状態参照パケット */
typedef struct t_rmpf {
    ID      wtskid;     /**< 獲得待ちタスクID */
    UINT    fblkcnt;    /**< 現在の空きブロック数 */
} T_RMPF;

/* ---------------------------------------------------------
 * サービスコール プロトタイプ宣言
 * --------------------------------------------------------- */

/* [1] タスク管理機能 */
ER cre_tsk(ID tskid, const T_CTSK *pk_ctsk);
ER del_tsk(ID tskid);
ER act_tsk(ID tskid);
ER iact_tsk(ID tskid);
ER_UINT can_act(ID tskid);
void ext_tsk(void);
void exd_tsk(void);
ER ter_tsk(ID tskid);
ER chg_pri(ID tskid, PRI tskpri);
ER get_pri(ID tskid, PRI *p_tskpri);
ER ref_tsk(ID tskid, T_RTSK *pk_rtsk);

/* [2] タスク付属同期機能 */
ER slp_tsk(void);
ER tslp_tsk(TMO tmout);
ER wup_tsk(ID tskid);
ER iwup_tsk(ID tskid);
ER_UINT can_wup(ID tskid);
ER rel_wai(ID tskid);
ER irel_wai(ID tskid);
ER sus_tsk(ID tskid);
ER rsm_tsk(ID tskid);
ER dly_tsk(RELTIM dlytim);

/* [3] セマフォ */
ER cre_sem(ID semid, const T_CSEM *pk_csem);
ER del_sem(ID semid);
ER sig_sem(ID semid);
ER isig_sem(ID semid);
ER wai_sem(ID semid);
ER pol_sem(ID semid);
ER twai_sem(ID semid, TMO tmout);
ER ref_sem(ID semid, T_RSEM *pk_rsem);

/* [4] イベントフラグ */
ER cre_flg(ID flgid, const T_CFLG *pk_cflg);
ER del_flg(ID flgid);
ER set_flg(ID flgid, UINT setptn);
ER iset_flg(ID flgid, UINT setptn);
ER clr_flg(ID flgid, UINT clrptn);
ER wai_flg(ID flgid, UINT waiptn, MODE wfmode, UINT *p_flgptn);
ER pol_flg(ID flgid, UINT waiptn, MODE wfmode, UINT *p_flgptn);
ER twai_flg(ID flgid, UINT waiptn, MODE wfmode, UINT *p_flgptn, TMO tmout);
ER ref_flg(ID flgid, T_RFLG *pk_rflg);

/* [5] メールボックス */
ER cre_mbx(ID mbxid, const T_CMBX *pk_cmbx);
ER del_mbx(ID mbxid);
ER snd_mbx(ID mbxid, T_MSG *pk_msg);
ER isnd_mbx(ID mbxid, T_MSG *pk_msg);
ER rcv_mbx(ID mbxid, T_MSG **ppk_msg);
ER prcv_mbx(ID mbxid, T_MSG **ppk_msg);
ER trcv_mbx(ID mbxid, T_MSG **ppk_msg, TMO tmout);
ER ref_mbx(ID mbxid, T_RMBX *pk_rmbx);

/* [6] 固定長メモリプール */
ER cre_mpf(ID mpfid, const T_CMPF *pk_cmpf);
ER del_mpf(ID mpfid);
ER get_mpf(ID mpfid, VP *p_blk);
ER pget_mpf(ID mpfid, VP *p_blk);
ER tget_mpf(ID mpfid, VP *p_blk, TMO tmout);
ER rel_mpf(ID mpfid, VP blk);
ER ref_mpf(ID mpfid, T_RMPF *pk_rmpf);

/* [7] システム時刻・時間管理 */
ER set_tim(const SYSTIM *p_systim);
ER get_tim(SYSTIM *p_systim);
ER isig_tim(void);

/* [8] システム状態管理・割込み制御 */
ER loc_cpu(void);
ER unl_cpu(void);
ER dis_dsp(void);
ER ena_dsp(void);
BOOL sns_ctx(void);
BOOL sns_loc(void);
BOOL sns_dsp(void);
BOOL sns_dpn(void);

#ifdef __cplusplus
}
#endif

#endif /* KERNEL_H */
