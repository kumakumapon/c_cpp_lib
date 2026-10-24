#include "uitron_internal.h"
#include <string.h>

/* ---------------------------------------------------------
 * タスク管理機能
 * --------------------------------------------------------- */
ER cre_tsk(ID tskid, const T_CTSK *pk_ctsk) {
    if (tskid <= 0 || tskid > UITRON_MAX_TSKID || !pk_ctsk) {
        return E_PAR;
    }
    if (pk_ctsk->itskpri <= 0 || pk_ctsk->itskpri > UITRON_MAX_PRI) {
        return E_PAR;
    }

    TCB *tcb = &g_kcb.tcbs[tskid];
    if (tcb->is_created) {
        return E_OBJ; /* 既に生成済み */
    }

    memset(tcb, 0, sizeof(TCB));
    tcb->tskid = tskid;
    tcb->is_created = TRUE;
    tcb->ctsk = *pk_ctsk;
    tcb->tskpri = pk_ctsk->itskpri;
    tcb->tskbpri = pk_ctsk->itskpri;
    tcb->tskstat = TTS_DMT; /* 休止状態 */

    /* TA_ACT属性があれば起動 */
    if (pk_ctsk->tskatr & TA_ACT) {
        tcb->tskstat = TTS_RDY;
        uitron_reschedule();
    }

    return E_OK;
}

ER del_tsk(ID tskid) {
    TCB *tcb = uitron_get_tcb(tskid);
    if (!tcb) return E_NOEXS;
    if (tcb->tskstat != TTS_DMT) return E_OBJ;

    tcb->is_created = FALSE;
    return E_OK;
}

ER act_tsk(ID tskid) {
    TCB *tcb = uitron_get_tcb(tskid);
    if (!tcb) return E_NOEXS;

    if (tcb->tskstat == TTS_DMT) {
        tcb->tskstat = TTS_RDY;
        uitron_reschedule();
    } else {
        if (tcb->actcnt < 255) {
            tcb->actcnt++;
        } else {
            return E_QOVR;
        }
    }
    return E_OK;
}

ER iact_tsk(ID tskid) {
    return act_tsk(tskid);
}

ER_UINT can_act(ID tskid) {
    TCB *tcb = uitron_get_tcb(tskid);
    if (!tcb) return E_NOEXS;

    UINT prev_cnt = tcb->actcnt;
    tcb->actcnt = 0;
    return (ER_UINT)prev_cnt;
}

/* 終了したタスクを休止状態へ戻し、起動要求がキューイング済みなら再起動する */
static void terminate_and_requeue(TCB *tcb) {
    tcb->tskstat = TTS_DMT;
    tcb->tskwait = 0;
    tcb->wid = 0;
    tcb->lefttmo = 0;
    tcb->wupcnt = 0;
    tcb->suscnt = 0;
    tcb->wait_er = E_OK; /* 次の実行に前回の待ち結果を持ち越さない */
    tcb->tskpri = tcb->tskbpri;
    if (tcb->actcnt > 0) {
        tcb->actcnt--;
        tcb->tskstat = TTS_RDY;
    }
}

void ext_tsk(void) {
    TCB *tcb = uitron_get_tcb(uitron_caller_tskid());
    if (tcb) {
        terminate_and_requeue(tcb);
        uitron_reschedule();
    }
}

void exd_tsk(void) {
    TCB *tcb = uitron_get_tcb(uitron_caller_tskid());
    if (tcb) {
        tcb->is_created = FALSE;
        tcb->tskstat = TTS_DMT;
        uitron_reschedule();
    }
}

ER ter_tsk(ID tskid) {
    TCB *tcb = uitron_get_tcb(tskid);
    if (!tcb) return E_NOEXS;
    if (tcb->tskstat == TTS_DMT) return E_OBJ;

    terminate_and_requeue(tcb);
    uitron_reschedule();
    return E_OK;
}

ER chg_pri(ID tskid, PRI tskpri) {
    TCB *tcb = uitron_get_tcb(tskid);
    if (!tcb) return E_NOEXS;
    if (tskpri != 0 && (tskpri < 1 || tskpri > UITRON_MAX_PRI)) return E_PAR;

    if (tskpri == 0) {
        tcb->tskpri = tcb->tskbpri;
    } else {
        tcb->tskpri = tskpri;
    }

    uitron_reschedule();
    return E_OK;
}

ER get_pri(ID tskid, PRI *p_tskpri) {
    if (!p_tskpri) return E_PAR;
    TCB *tcb = uitron_get_tcb(tskid);
    if (!tcb) return E_NOEXS;
    if (tcb->tskstat == TTS_DMT) return E_OBJ;

    *p_tskpri = tcb->tskpri;
    return E_OK;
}

ER ref_tsk(ID tskid, T_RTSK *pk_rtsk) {
    if (!pk_rtsk) return E_PAR;
    TCB *tcb = uitron_get_tcb(tskid);
    if (!tcb) return E_NOEXS;

    pk_rtsk->tskstat = tcb->tskstat;
    pk_rtsk->tskpri  = tcb->tskpri;
    pk_rtsk->tskbpri = tcb->tskbpri;
    pk_rtsk->tskwait = tcb->tskwait;
    pk_rtsk->wid     = tcb->wid;
    pk_rtsk->lefttmo = tcb->lefttmo;
    pk_rtsk->actcnt  = tcb->actcnt;
    pk_rtsk->wupcnt  = tcb->wupcnt;
    pk_rtsk->suscnt  = tcb->suscnt;
    return E_OK;
}

/* ---------------------------------------------------------
 * タスク付属同期機能
 * --------------------------------------------------------- */
ER slp_tsk(void) {
    return tslp_tsk(TMO_FEVR);
}

ER tslp_tsk(TMO tmout) {
    TCB *cur = uitron_get_tcb(uitron_caller_tskid());
    if (!cur) return E_CTX;

    if (cur->wupcnt > 0) {
        cur->wupcnt--;
        return E_OK;
    }

    if (tmout == TMO_POL) {
        return E_TMOUT;
    }

    uitron_task_wait(cur, TTW_SLP, 0, tmout);
    uitron_reschedule();
    return E_WBLK; /* 呼出元は中断できない: 待ち登録済み (結果は ref_tsk/コールバックで確認) */
}

ER wup_tsk(ID tskid) {
    TCB *tcb = uitron_get_tcb(tskid);
    if (!tcb) return E_NOEXS;
    if (tcb->tskstat == TTS_DMT) return E_OBJ;

    if (tcb->tskstat == TTS_WAI && tcb->tskwait == TTW_SLP) {
        uitron_task_wakeup(tcb, E_OK);
        uitron_reschedule();
    } else {
        if (tcb->wupcnt < 255) {
            tcb->wupcnt++;
        } else {
            return E_QOVR;
        }
    }
    return E_OK;
}

ER iwup_tsk(ID tskid) {
    return wup_tsk(tskid);
}

ER_UINT can_wup(ID tskid) {
    TCB *tcb = uitron_get_tcb(tskid);
    if (!tcb) return E_NOEXS;

    UINT prev = tcb->wupcnt;
    tcb->wupcnt = 0;
    return (ER_UINT)prev;
}

ER rel_wai(ID tskid) {
    TCB *tcb = uitron_get_tcb(tskid);
    if (!tcb) return E_NOEXS;
    if (tcb->tskstat != TTS_WAI && tcb->tskstat != TTS_WAS) return E_OBJ;

    if (tcb->tskstat == TTS_WAI) {
        uitron_task_wakeup(tcb, E_RLWAI);
    } else {
        tcb->tskstat = TTS_SUS;
        tcb->tskwait = 0;
        tcb->wid = 0;
        tcb->wait_er = E_RLWAI;
    }
    uitron_reschedule();
    return E_OK;
}

ER irel_wai(ID tskid) {
    return rel_wai(tskid);
}

ER sus_tsk(ID tskid) {
    TCB *tcb = uitron_get_tcb(tskid);
    if (!tcb) return E_NOEXS;
    if (tcb->tskstat == TTS_DMT) return E_OBJ;

    if (tcb->tskstat == TTS_RUN || tcb->tskstat == TTS_RDY) {
        tcb->tskstat = TTS_SUS;
        tcb->suscnt = 1;
        uitron_reschedule();
    } else if (tcb->tskstat == TTS_WAI) {
        tcb->tskstat = TTS_WAS;
        tcb->suscnt = 1;
    } else if (tcb->tskstat == TTS_SUS || tcb->tskstat == TTS_WAS) {
        if (tcb->suscnt < 255) {
            tcb->suscnt++;
        } else {
            return E_QOVR;
        }
    }
    return E_OK;
}

ER rsm_tsk(ID tskid) {
    TCB *tcb = uitron_get_tcb(tskid);
    if (!tcb) return E_NOEXS;
    if (tcb->tskstat != TTS_SUS && tcb->tskstat != TTS_WAS) return E_OBJ;

    tcb->suscnt--;
    if (tcb->suscnt == 0) {
        if (tcb->tskstat == TTS_SUS) {
            tcb->tskstat = TTS_RDY;
            uitron_reschedule();
        } else {
            tcb->tskstat = TTS_WAI;
        }
    }
    return E_OK;
}

ER dly_tsk(RELTIM dlytim) {
    TCB *cur = uitron_get_tcb(uitron_caller_tskid());
    if (!cur) return E_CTX;
    if (dlytim == 0) return E_OK;

    uitron_task_wait(cur, TTW_DLY, 0, dlytim);
    uitron_reschedule();
    return E_WBLK; /* 呼出元は中断できない: 待ち登録済み (結果は ref_tsk/コールバックで確認) */
}
