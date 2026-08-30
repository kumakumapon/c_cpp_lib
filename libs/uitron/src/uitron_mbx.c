#include "uitron_internal.h"
#include <string.h>

ER cre_mbx(ID mbxid, const T_CMBX *pk_cmbx) {
    if (mbxid <= 0 || mbxid > UITRON_MAX_MBXID || !pk_cmbx) {
        return E_PAR;
    }

    MCB *mcb = &g_kcb.mcbs[mbxid];
    if (mcb->is_created) {
        return E_OBJ;
    }

    memset(mcb, 0, sizeof(MCB));
    mcb->mbxid = mbxid;
    mcb->is_created = TRUE;
    mcb->cmbx = *pk_cmbx;
    mcb->head = NULL;
    mcb->tail = NULL;
    return E_OK;
}

ER del_mbx(ID mbxid) {
    if (mbxid <= 0 || mbxid > UITRON_MAX_MBXID) return E_ID;
    MCB *mcb = &g_kcb.mcbs[mbxid];
    if (!mcb->is_created) return E_NOEXS;

    for (ID id = 1; id <= UITRON_MAX_TSKID; ++id) {
        TCB *tcb = &g_kcb.tcbs[id];
        if (tcb->is_created && (tcb->tskstat == TTS_WAI || tcb->tskstat == TTS_WAS)) {
            if (tcb->tskwait == TTW_MBX && tcb->wid == mbxid) {
                uitron_task_wakeup(tcb, E_DLT);
            }
        }
    }

    mcb->is_created = FALSE;
    uitron_reschedule();
    return E_OK;
}

ER snd_mbx(ID mbxid, T_MSG *pk_msg) {
    if (mbxid <= 0 || mbxid > UITRON_MAX_MBXID || !pk_msg) return E_PAR;
    MCB *mcb = &g_kcb.mcbs[mbxid];
    if (!mcb->is_created) return E_NOEXS;

    pk_msg->next = NULL;

    /* 待ちタスクが存在するかチェック */
    TCB *wait_tcb = NULL;
    PRI highest_pri = UITRON_MAX_PRI + 1;

    for (ID id = 1; id <= UITRON_MAX_TSKID; ++id) {
        TCB *tcb = &g_kcb.tcbs[id];
        if (tcb->is_created && (tcb->tskstat == TTS_WAI || tcb->tskstat == TTS_WAS)) {
            if (tcb->tskwait == TTW_MBX && tcb->wid == mbxid) {
                if (mcb->cmbx.mbxatr & TA_TPRI) {
                    if (tcb->tskpri < highest_pri) {
                        highest_pri = tcb->tskpri;
                        wait_tcb = tcb;
                    }
                } else {
                    wait_tcb = tcb;
                    break;
                }
            }
        }
    }

    if (wait_tcb) {
        if (wait_tcb->ppk_msg) {
            *wait_tcb->ppk_msg = pk_msg;
        }
        uitron_task_wakeup(wait_tcb, E_OK);
        uitron_reschedule();
    } else {
        /* キューに挿入 (FIFO) */
        if (!mcb->head) {
            mcb->head = pk_msg;
            mcb->tail = pk_msg;
        } else {
            mcb->tail->next = pk_msg;
            mcb->tail = pk_msg;
        }
    }

    return E_OK;
}

ER isnd_mbx(ID mbxid, T_MSG *pk_msg) {
    return snd_mbx(mbxid, pk_msg);
}

ER rcv_mbx(ID mbxid, T_MSG **ppk_msg) {
    return trcv_mbx(mbxid, ppk_msg, TMO_FEVR);
}

ER prcv_mbx(ID mbxid, T_MSG **ppk_msg) {
    return trcv_mbx(mbxid, ppk_msg, TMO_POL);
}

ER trcv_mbx(ID mbxid, T_MSG **ppk_msg, TMO tmout) {
    if (mbxid <= 0 || mbxid > UITRON_MAX_MBXID || !ppk_msg) return E_PAR;
    MCB *mcb = &g_kcb.mcbs[mbxid];
    if (!mcb->is_created) return E_NOEXS;

    TCB *cur = uitron_get_tcb(g_kcb.cur_tskid);
    if (!cur) return E_CTX;

    if (mcb->head) {
        *ppk_msg = mcb->head;
        mcb->head = mcb->head->next;
        if (!mcb->head) {
            mcb->tail = NULL;
        }
        return E_OK;
    }

    if (tmout == TMO_POL) {
        return E_TMOUT;
    }

    cur->ppk_msg = ppk_msg;
    uitron_task_wait(cur, TTW_MBX, mbxid, tmout);
    uitron_reschedule();
    return cur->wait_er;
}

ER ref_mbx(ID mbxid, T_RMBX *pk_rmbx) {
    if (!pk_rmbx) return E_PAR;
    if (mbxid <= 0 || mbxid > UITRON_MAX_MBXID) return E_ID;
    MCB *mcb = &g_kcb.mcbs[mbxid];
    if (!mcb->is_created) return E_NOEXS;

    pk_rmbx->pk_msg = mcb->head;
    pk_rmbx->wtskid = TSK_SELF;

    for (ID id = 1; id <= UITRON_MAX_TSKID; ++id) {
        TCB *tcb = &g_kcb.tcbs[id];
        if (tcb->is_created && (tcb->tskstat == TTS_WAI || tcb->tskstat == TTS_WAS)) {
            if (tcb->tskwait == TTW_MBX && tcb->wid == mbxid) {
                pk_rmbx->wtskid = id;
                break;
            }
        }
    }
    return E_OK;
}
