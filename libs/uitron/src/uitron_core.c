#include "uitron_internal.h"
#include <string.h>

KERNEL_CB g_kcb;

void uitron_os_init(void) {
    memset(&g_kcb, 0, sizeof(g_kcb));
    g_kcb.dsp_enabled = TRUE;
    g_kcb.cpu_locked = FALSE;
    g_kcb.cur_tskid = TSK_SELF;
    g_kcb.systim = 0;
}

void uitron_os_shutdown(void) {
    uitron_os_init();
}

/* サービスコールを呼び出したタスクのID。
 * エントリ関数の実行中は、そのタスク自身を指す。サービスコール内のディスパッチ
 * (より高優先度のタスクを起こした場合など) でスケジューラの選択が変わっても、
 * 呼出元は変わらない。タスク外 (割込みハンドラ相当) では実行状態のタスクを指す。 */
ID uitron_caller_tskid(void) {
    return (g_kcb.exec_tskid != TSK_SELF) ? g_kcb.exec_tskid : g_kcb.cur_tskid;
}

TCB *uitron_get_tcb(ID tskid) {
    if (tskid == TSK_SELF) {
        tskid = uitron_caller_tskid();
    }
    if (tskid <= 0 || tskid > UITRON_MAX_TSKID) {
        return NULL;
    }
    TCB *tcb = &g_kcb.tcbs[tskid];
    return tcb->is_created ? tcb : NULL;
}

void uitron_task_ready(TCB *tcb) {
    if (!tcb) return;
    tcb->tskstat = TTS_RDY;
    tcb->tskwait = 0;
    tcb->wid = 0;
    tcb->lefttmo = 0;
}

void uitron_task_wait(TCB *tcb, STAT tskwait, ID wid, TMO tmout) {
    if (!tcb) return;
    tcb->tskstat = TTS_WAI;
    tcb->tskwait = tskwait;
    tcb->wid = wid;
    tcb->lefttmo = tmout;
    tcb->wait_er = E_OK;
    tcb->wait_seq = ++g_kcb.wait_seq_counter;
}

/* 指定オブジェクトを待っているタスクのうち、次に解除すべきタスクを返す。
 * by_priority (TA_TPRI) なら優先度順 (同一優先度は待ちに入った順)、
 * そうでなければ (TA_TFIFO) 待ちに入った順。 */
TCB *uitron_pick_waiter(STAT tskwait, ID wid, BOOL by_priority) {
    TCB *best = NULL;
    for (ID id = 1; id <= UITRON_MAX_TSKID; ++id) {
        TCB *tcb = &g_kcb.tcbs[id];
        if (!tcb->is_created || (tcb->tskstat != TTS_WAI && tcb->tskstat != TTS_WAS)) continue;
        if (tcb->tskwait != tskwait || tcb->wid != wid) continue;
        if (!best) {
            best = tcb;
        } else if (by_priority && tcb->tskpri != best->tskpri) {
            if (tcb->tskpri < best->tskpri) best = tcb;
        } else if (tcb->wait_seq < best->wait_seq) {
            best = tcb;
        }
    }
    return best;
}

void uitron_task_wakeup(TCB *tcb, ER er) {
    if (!tcb) return;
    /* 二重待ち (待ち+サスペンド) は、待ち解除後もサスペンドを維持する */
    tcb->tskstat = (tcb->tskstat == TTS_WAS) ? TTS_SUS : TTS_RDY;
    tcb->tskwait = 0;
    tcb->wid = 0;
    tcb->lefttmo = 0;
    tcb->wait_er = er;
}

void uitron_reschedule(void) {
    if (!g_kcb.dsp_enabled || g_kcb.cpu_locked) {
        return;
    }

    /* 最高優先度のREADYタスクを探索 */
    PRI highest_pri = UITRON_MAX_PRI + 1;
    ID best_tskid = TSK_SELF;

    for (ID id = 1; id <= UITRON_MAX_TSKID; ++id) {
        TCB *tcb = &g_kcb.tcbs[id];
        if (tcb->is_created && (tcb->tskstat == TTS_RDY || tcb->tskstat == TTS_RUN)) {
            if (tcb->tskpri < highest_pri) {
                highest_pri = tcb->tskpri;
                best_tskid = id;
            }
        }
    }

    if (best_tskid != TSK_SELF) {
        if (g_kcb.cur_tskid != TSK_SELF && g_kcb.cur_tskid != best_tskid) {
            TCB *prev = uitron_get_tcb(g_kcb.cur_tskid);
            if (prev && prev->tskstat == TTS_RUN) {
                prev->tskstat = TTS_RDY;
            }
        }
        g_kcb.cur_tskid = best_tskid;
        g_kcb.tcbs[best_tskid].tskstat = TTS_RUN;
    } else {
        g_kcb.cur_tskid = TSK_SELF;
    }
}

void uitron_os_schedule(void) {
    uitron_reschedule();
}

ID uitron_os_get_current_task(void) {
    return g_kcb.cur_tskid;
}

void uitron_os_tick(RELTIM ms) {
    g_kcb.systim += ms;

    /* タイムアウト処理 */
    for (ID id = 1; id <= UITRON_MAX_TSKID; ++id) {
        TCB *tcb = &g_kcb.tcbs[id];
        if (tcb->is_created && (tcb->tskstat == TTS_WAI || tcb->tskstat == TTS_WAS)) {
            if (tcb->lefttmo != TMO_FEVR && tcb->lefttmo > 0) {
                if (tcb->lefttmo <= ms) {
                    /* タイムアウト発生 */
                    uitron_task_wakeup(tcb, tcb->tskwait == TTW_DLY ? E_OK : E_TMOUT);
                } else {
                    tcb->lefttmo -= ms;
                }
            }
        }
    }

    uitron_reschedule();
}

void uitron_os_run_step(void) {
    uitron_reschedule();
    if (g_kcb.cur_tskid != TSK_SELF) {
        TCB *tcb = uitron_get_tcb(g_kcb.cur_tskid);
        if (tcb && tcb->ctsk.task) {
            const ID prev_exec = g_kcb.exec_tskid;
            g_kcb.exec_tskid = tcb->tskid;
            tcb->ctsk.task();
            g_kcb.exec_tskid = prev_exec;
        }
    }
}

/* ---------------------------------------------------------
 * システム時刻・時間管理 API
 * --------------------------------------------------------- */
ER set_tim(const SYSTIM *p_systim) {
    if (!p_systim) return E_PAR;
    g_kcb.systim = *p_systim;
    return E_OK;
}

ER get_tim(SYSTIM *p_systim) {
    if (!p_systim) return E_PAR;
    *p_systim = g_kcb.systim;
    return E_OK;
}

ER isig_tim(void) {
    uitron_os_tick(1);
    return E_OK;
}

/* ---------------------------------------------------------
 * システム状態管理 API
 * --------------------------------------------------------- */
ER loc_cpu(void) {
    g_kcb.cpu_locked = TRUE;
    return E_OK;
}

ER unl_cpu(void) {
    g_kcb.cpu_locked = FALSE;
    uitron_reschedule();
    return E_OK;
}

ER dis_dsp(void) {
    g_kcb.dsp_enabled = FALSE;
    return E_OK;
}

ER ena_dsp(void) {
    g_kcb.dsp_enabled = TRUE;
    uitron_reschedule();
    return E_OK;
}

BOOL sns_ctx(void) {
    return FALSE; /* タスクコンテキスト */
}

BOOL sns_loc(void) {
    return g_kcb.cpu_locked;
}

BOOL sns_dsp(void) {
    return !g_kcb.dsp_enabled;
}

BOOL sns_dpn(void) {
    return (g_kcb.cpu_locked || !g_kcb.dsp_enabled);
}
