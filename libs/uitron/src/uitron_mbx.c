#include "uitron_internal.h"
#include <string.h>

ER cre_mbx(ID mbxid, const T_CMBX *pk_cmbx) {
    if (mbxid <= 0 || mbxid > UITRON_MAX_MBXID) {
        return E_ID;
    }
    if (!pk_cmbx) {
        return E_PAR;
    }
    /* メッセージ優先度順では、送れる優先度の範囲 (1..maxmpri) が必要 */
    if ((pk_cmbx->mbxatr & TA_MPRI) && pk_cmbx->maxmpri < 1) {
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
    if (mbxid <= 0 || mbxid > UITRON_MAX_MBXID) return E_ID;
    if (!pk_msg) return E_PAR;
    MCB *mcb = &g_kcb.mcbs[mbxid];
    if (!mcb->is_created) return E_NOEXS;

    const BOOL by_msgpri = (mcb->cmbx.mbxatr & TA_MPRI) != 0;
    PRI msgpri = 0;
    if (by_msgpri) {
        msgpri = ((T_MSG_PRI *)pk_msg)->msgpri;
        if (msgpri < 1 || msgpri > mcb->cmbx.maxmpri) return E_PAR;
    }

    pk_msg->next = NULL;

    TCB *wait_tcb = uitron_pick_waiter(TTW_MBX, mbxid, (mcb->cmbx.mbxatr & TA_TPRI) != 0);

    if (wait_tcb) {
        if (wait_tcb->ppk_msg) {
            *wait_tcb->ppk_msg = pk_msg;
        }
        uitron_task_wakeup(wait_tcb, E_OK);
        uitron_reschedule();
    } else {
        if (!mcb->head) {
            mcb->head = pk_msg;
            mcb->tail = pk_msg;
        } else if (by_msgpri && ((T_MSG_PRI *)mcb->head)->msgpri > msgpri) {
            /* 先頭より優先度が高い (値が小さい) */
            pk_msg->next = mcb->head;
            mcb->head = pk_msg;
        } else if (by_msgpri) {
            /* 同じ優先度のメッセージは送信順になるよう、自分より大きい値の直前に挿入 */
            T_MSG *prev = mcb->head;
            while (prev->next && ((T_MSG_PRI *)prev->next)->msgpri <= msgpri) {
                prev = prev->next;
            }
            pk_msg->next = prev->next;
            prev->next = pk_msg;
            if (!pk_msg->next) {
                mcb->tail = pk_msg;
            }
        } else {
            /* キューの末尾へ (FIFO) */
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
    if (mbxid <= 0 || mbxid > UITRON_MAX_MBXID) return E_ID;
    if (!ppk_msg) return E_PAR;
    MCB *mcb = &g_kcb.mcbs[mbxid];
    if (!mcb->is_created) return E_NOEXS;

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

    TCB *cur = uitron_get_tcb(uitron_caller_tskid());
    if (!cur) return E_CTX;

    cur->ppk_msg = ppk_msg;
    uitron_task_wait(cur, TTW_MBX, mbxid, tmout);
    uitron_reschedule();
    return E_WBLK; /* 呼出元は中断できない: 待ち登録済み (結果は ref_tsk/コールバックで確認) */
}

ER ref_mbx(ID mbxid, T_RMBX *pk_rmbx) {
    if (!pk_rmbx) return E_PAR;
    if (mbxid <= 0 || mbxid > UITRON_MAX_MBXID) return E_ID;
    MCB *mcb = &g_kcb.mcbs[mbxid];
    if (!mcb->is_created) return E_NOEXS;

    pk_rmbx->pk_msg = mcb->head;
    /* 待ち行列の先頭 (属性に従った次に解除されるタスク) */
    TCB *head = uitron_pick_waiter(TTW_MBX, mbxid, (mcb->cmbx.mbxatr & TA_TPRI) != 0);
    pk_rmbx->wtskid = head ? head->tskid : TSK_SELF;
    return E_OK;
}
