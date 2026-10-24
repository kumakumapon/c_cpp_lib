#include "uitron_internal.h"
#include <string.h>

ER cre_sem(ID semid, const T_CSEM *pk_csem) {
    if (semid <= 0 || semid > UITRON_MAX_SEMID || !pk_csem) {
        return E_PAR;
    }
    if (pk_csem->maxsem == 0 || pk_csem->isemcnt > pk_csem->maxsem) {
        return E_PAR;
    }

    SCB *scb = &g_kcb.scbs[semid];
    if (scb->is_created) {
        return E_OBJ;
    }

    memset(scb, 0, sizeof(SCB));
    scb->semid = semid;
    scb->is_created = TRUE;
    scb->csem = *pk_csem;
    scb->semcnt = pk_csem->isemcnt;
    return E_OK;
}

ER del_sem(ID semid) {
    if (semid <= 0 || semid > UITRON_MAX_SEMID) return E_ID;
    SCB *scb = &g_kcb.scbs[semid];
    if (!scb->is_created) return E_NOEXS;

    /* 待ちタスクをすべてE_DLTで解除 */
    for (ID id = 1; id <= UITRON_MAX_TSKID; ++id) {
        TCB *tcb = &g_kcb.tcbs[id];
        if (tcb->is_created && (tcb->tskstat == TTS_WAI || tcb->tskstat == TTS_WAS)) {
            if (tcb->tskwait == TTW_SEM && tcb->wid == semid) {
                uitron_task_wakeup(tcb, E_DLT);
            }
        }
    }

    scb->is_created = FALSE;
    uitron_reschedule();
    return E_OK;
}

ER sig_sem(ID semid) {
    if (semid <= 0 || semid > UITRON_MAX_SEMID) return E_ID;
    SCB *scb = &g_kcb.scbs[semid];
    if (!scb->is_created) return E_NOEXS;

    TCB *wait_tcb = uitron_pick_waiter(TTW_SEM, semid, (scb->csem.sematr & TA_TPRI) != 0);

    if (wait_tcb) {
        uitron_task_wakeup(wait_tcb, E_OK);
        uitron_reschedule();
    } else {
        if (scb->semcnt < scb->csem.maxsem) {
            scb->semcnt++;
        } else {
            return E_QOVR;
        }
    }

    return E_OK;
}

ER isig_sem(ID semid) {
    return sig_sem(semid);
}

ER wai_sem(ID semid) {
    return twai_sem(semid, TMO_FEVR);
}

ER pol_sem(ID semid) {
    return twai_sem(semid, TMO_POL);
}

ER twai_sem(ID semid, TMO tmout) {
    if (semid <= 0 || semid > UITRON_MAX_SEMID) return E_ID;
    SCB *scb = &g_kcb.scbs[semid];
    if (!scb->is_created) return E_NOEXS;

    if (scb->semcnt > 0) {
        scb->semcnt--;
        return E_OK;
    }

    if (tmout == TMO_POL) {
        return E_TMOUT;
    }

    TCB *cur = uitron_get_tcb(uitron_caller_tskid());
    if (!cur) return E_CTX;

    uitron_task_wait(cur, TTW_SEM, semid, tmout);
    uitron_reschedule();
    return E_WBLK; /* 呼出元は中断できない: 待ち登録済み (結果は ref_tsk/コールバックで確認) */
}

ER ref_sem(ID semid, T_RSEM *pk_rsem) {
    if (!pk_rsem) return E_PAR;
    if (semid <= 0 || semid > UITRON_MAX_SEMID) return E_ID;
    SCB *scb = &g_kcb.scbs[semid];
    if (!scb->is_created) return E_NOEXS;

    pk_rsem->semcnt = scb->semcnt;
    pk_rsem->wtskid = TSK_SELF;

    for (ID id = 1; id <= UITRON_MAX_TSKID; ++id) {
        TCB *tcb = &g_kcb.tcbs[id];
        if (tcb->is_created && (tcb->tskstat == TTS_WAI || tcb->tskstat == TTS_WAS)) {
            if (tcb->tskwait == TTW_SEM && tcb->wid == semid) {
                pk_rsem->wtskid = id;
                break;
            }
        }
    }
    return E_OK;
}
