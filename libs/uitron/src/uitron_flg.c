#include "uitron_internal.h"
#include <string.h>

static BOOL check_flg_cond(UINT cur_ptn, UINT waiptn, MODE wfmode) {
    if (wfmode & TWF_ORW) {
        return (cur_ptn & waiptn) != 0;
    } else {
        /* TWF_ANDW */
        return (cur_ptn & waiptn) == waiptn;
    }
}

ER cre_flg(ID flgid, const T_CFLG *pk_cflg) {
    if (flgid <= 0 || flgid > UITRON_MAX_FLGID || !pk_cflg) {
        return E_PAR;
    }

    FCB *fcb = &g_kcb.fcbs[flgid];
    if (fcb->is_created) {
        return E_OBJ;
    }

    memset(fcb, 0, sizeof(FCB));
    fcb->flgid = flgid;
    fcb->is_created = TRUE;
    fcb->cflg = *pk_cflg;
    fcb->flgptn = pk_cflg->iflgptn;
    return E_OK;
}

ER del_flg(ID flgid) {
    if (flgid <= 0 || flgid > UITRON_MAX_FLGID) return E_ID;
    FCB *fcb = &g_kcb.fcbs[flgid];
    if (!fcb->is_created) return E_NOEXS;

    for (ID id = 1; id <= UITRON_MAX_TSKID; ++id) {
        TCB *tcb = &g_kcb.tcbs[id];
        if (tcb->is_created && (tcb->tskstat == TTS_WAI || tcb->tskstat == TTS_WAS)) {
            if (tcb->tskwait == TTW_FLG && tcb->wid == flgid) {
                uitron_task_wakeup(tcb, E_DLT);
            }
        }
    }

    fcb->is_created = FALSE;
    uitron_reschedule();
    return E_OK;
}

static void check_flg_waiters(FCB *fcb) {
    for (ID id = 1; id <= UITRON_MAX_TSKID; ++id) {
        TCB *tcb = &g_kcb.tcbs[id];
        if (tcb->is_created && (tcb->tskstat == TTS_WAI || tcb->tskstat == TTS_WAS)) {
            if (tcb->tskwait == TTW_FLG && tcb->wid == fcb->flgid) {
                if (check_flg_cond(fcb->flgptn, tcb->waiptn, tcb->wfmode)) {
                    if (tcb->p_flgptn) {
                        *tcb->p_flgptn = fcb->flgptn;
                    }
                    if ((tcb->wfmode & TWF_CLR) || (fcb->cflg.flgatr & TA_CLR)) {
                        fcb->flgptn = 0;
                    }
                    uitron_task_wakeup(tcb, E_OK);
                    if (!(fcb->cflg.flgatr & TA_WMUL)) {
                        break; /* 単一タスク待ち */
                    }
                }
            }
        }
    }
}

ER set_flg(ID flgid, UINT setptn) {
    if (flgid <= 0 || flgid > UITRON_MAX_FLGID) return E_ID;
    FCB *fcb = &g_kcb.fcbs[flgid];
    if (!fcb->is_created) return E_NOEXS;

    fcb->flgptn |= setptn;
    check_flg_waiters(fcb);
    uitron_reschedule();
    return E_OK;
}

ER iset_flg(ID flgid, UINT setptn) {
    return set_flg(flgid, setptn);
}

ER clr_flg(ID flgid, UINT clrptn) {
    if (flgid <= 0 || flgid > UITRON_MAX_FLGID) return E_ID;
    FCB *fcb = &g_kcb.fcbs[flgid];
    if (!fcb->is_created) return E_NOEXS;

    fcb->flgptn &= clrptn;
    return E_OK;
}

ER wai_flg(ID flgid, UINT waiptn, MODE wfmode, UINT *p_flgptn) {
    return twai_flg(flgid, waiptn, wfmode, p_flgptn, TMO_FEVR);
}

ER pol_flg(ID flgid, UINT waiptn, MODE wfmode, UINT *p_flgptn) {
    return twai_flg(flgid, waiptn, wfmode, p_flgptn, TMO_POL);
}

ER twai_flg(ID flgid, UINT waiptn, MODE wfmode, UINT *p_flgptn, TMO tmout) {
    if (flgid <= 0 || flgid > UITRON_MAX_FLGID || waiptn == 0) return E_PAR;
    FCB *fcb = &g_kcb.fcbs[flgid];
    if (!fcb->is_created) return E_NOEXS;

    if (check_flg_cond(fcb->flgptn, waiptn, wfmode)) {
        if (p_flgptn) {
            *p_flgptn = fcb->flgptn;
        }
        if ((wfmode & TWF_CLR) || (fcb->cflg.flgatr & TA_CLR)) {
            fcb->flgptn = 0;
        }
        return E_OK;
    }

    if (tmout == TMO_POL) {
        return E_TMOUT;
    }

    TCB *cur = uitron_get_tcb(g_kcb.cur_tskid);
    if (!cur) return E_CTX;

    cur->waiptn = waiptn;
    cur->wfmode = wfmode;
    cur->p_flgptn = p_flgptn;

    uitron_task_wait(cur, TTW_FLG, flgid, tmout);
    uitron_reschedule();
    return E_WBLK; /* 呼出元は中断できない: 待ち登録済み (結果は ref_tsk/コールバックで確認) */
}

ER ref_flg(ID flgid, T_RFLG *pk_rflg) {
    if (!pk_rflg) return E_PAR;
    if (flgid <= 0 || flgid > UITRON_MAX_FLGID) return E_ID;
    FCB *fcb = &g_kcb.fcbs[flgid];
    if (!fcb->is_created) return E_NOEXS;

    pk_rflg->flgptn = fcb->flgptn;
    pk_rflg->wtskid = TSK_SELF;

    for (ID id = 1; id <= UITRON_MAX_TSKID; ++id) {
        TCB *tcb = &g_kcb.tcbs[id];
        if (tcb->is_created && (tcb->tskstat == TTS_WAI || tcb->tskstat == TTS_WAS)) {
            if (tcb->tskwait == TTW_FLG && tcb->wid == flgid) {
                pk_rflg->wtskid = id;
                break;
            }
        }
    }
    return E_OK;
}
