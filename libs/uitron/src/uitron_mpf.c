#include "uitron_internal.h"
#include <string.h>

#define MPF_ALIGN (sizeof(void*))
#define ALIGN_UP(size, align) (((size) + ((align) - 1)) & ~((align) - 1))

/* 静的フォールバック用バッファプール (NULL指定時) */
static uint8_t s_mpf_fallback_pool[UITRON_MAX_MPFID + 1][2048];

ER cre_mpf(ID mpfid, const T_CMPF *pk_cmpf) {
    if (mpfid <= 0 || mpfid > UITRON_MAX_MPFID || !pk_cmpf) {
        return E_PAR;
    }
    if (pk_cmpf->blkcnt == 0 || pk_cmpf->blksz == 0) {
        return E_PAR;
    }

    PCB *pcb = &g_kcb.pcbs[mpfid];
    if (pcb->is_created) {
        return E_OBJ;
    }

    size_t actual_blksz = ALIGN_UP(pk_cmpf->blksz, MPF_ALIGN);
    if (actual_blksz < sizeof(void*)) {
        actual_blksz = sizeof(void*);
    }

    uint8_t *mem = (uint8_t *)pk_cmpf->mpf;
    if (!mem) {
        if (actual_blksz * pk_cmpf->blkcnt > sizeof(s_mpf_fallback_pool[mpfid])) {
            return E_NOMEM;
        }
        mem = s_mpf_fallback_pool[mpfid];
        pcb->is_allocated_internally = TRUE;
    } else {
        pcb->is_allocated_internally = FALSE;
    }

    memset(pcb, 0, sizeof(PCB));
    pcb->mpfid = mpfid;
    pcb->is_created = TRUE;
    pcb->cmpf = *pk_cmpf;
    pcb->cmpf.blksz = (UINT)actual_blksz;
    pcb->pool_mem = mem;
    pcb->fblkcnt = pk_cmpf->blkcnt;

    /* フリーリストの初期化 */
    pcb->free_list = mem;
    for (UINT i = 0; i < pk_cmpf->blkcnt; ++i) {
        uint8_t *curr = mem + (i * actual_blksz);
        uint8_t *next = (i + 1 < pk_cmpf->blkcnt) ? (mem + ((i + 1) * actual_blksz)) : NULL;
        *(void **)curr = next;
    }

    return E_OK;
}

ER del_mpf(ID mpfid) {
    if (mpfid <= 0 || mpfid > UITRON_MAX_MPFID) return E_ID;
    PCB *pcb = &g_kcb.pcbs[mpfid];
    if (!pcb->is_created) return E_NOEXS;

    for (ID id = 1; id <= UITRON_MAX_TSKID; ++id) {
        TCB *tcb = &g_kcb.tcbs[id];
        if (tcb->is_created && (tcb->tskstat == TTS_WAI || tcb->tskstat == TTS_WAS)) {
            if (tcb->tskwait == TTW_MPF && tcb->wid == mpfid) {
                uitron_task_wakeup(tcb, E_DLT);
            }
        }
    }

    pcb->is_created = FALSE;
    uitron_reschedule();
    return E_OK;
}

ER get_mpf(ID mpfid, VP *p_blk) {
    return tget_mpf(mpfid, p_blk, TMO_FEVR);
}

ER pget_mpf(ID mpfid, VP *p_blk) {
    return tget_mpf(mpfid, p_blk, TMO_POL);
}

ER tget_mpf(ID mpfid, VP *p_blk, TMO tmout) {
    if (mpfid <= 0 || mpfid > UITRON_MAX_MPFID || !p_blk) return E_PAR;
    PCB *pcb = &g_kcb.pcbs[mpfid];
    if (!pcb->is_created) return E_NOEXS;

    if (pcb->fblkcnt > 0 && pcb->free_list) {
        void *blk = pcb->free_list;
        pcb->free_list = *(uint8_t **)blk;
        pcb->fblkcnt--;
        *p_blk = blk;
        return E_OK;
    }

    if (tmout == TMO_POL) {
        return E_TMOUT;
    }

    TCB *cur = uitron_get_tcb(g_kcb.cur_tskid);
    if (!cur) return E_CTX;

    cur->p_mpf_blk = p_blk;
    uitron_task_wait(cur, TTW_MPF, mpfid, tmout);
    uitron_reschedule();
    return cur->wait_er;
}

ER rel_mpf(ID mpfid, VP blk) {
    if (mpfid <= 0 || mpfid > UITRON_MAX_MPFID || !blk) return E_PAR;
    PCB *pcb = &g_kcb.pcbs[mpfid];
    if (!pcb->is_created) return E_NOEXS;

    /* 待ちタスクが存在するか探索 */
    TCB *wait_tcb = NULL;
    PRI highest_pri = UITRON_MAX_PRI + 1;

    for (ID id = 1; id <= UITRON_MAX_TSKID; ++id) {
        TCB *tcb = &g_kcb.tcbs[id];
        if (tcb->is_created && (tcb->tskstat == TTS_WAI || tcb->tskstat == TTS_WAS)) {
            if (tcb->tskwait == TTW_MPF && tcb->wid == mpfid) {
                if (pcb->cmpf.mpfatr & TA_TPRI) {
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
        if (wait_tcb->p_mpf_blk) {
            *wait_tcb->p_mpf_blk = blk;
        }
        uitron_task_wakeup(wait_tcb, E_OK);
        uitron_reschedule();
    } else {
        /* フリーリストへ返却 */
        *(void **)blk = pcb->free_list;
        pcb->free_list = (uint8_t *)blk;
        pcb->fblkcnt++;
    }

    return E_OK;
}

ER ref_mpf(ID mpfid, T_RMPF *pk_rmpf) {
    if (!pk_rmpf) return E_PAR;
    if (mpfid <= 0 || mpfid > UITRON_MAX_MPFID) return E_ID;
    PCB *pcb = &g_kcb.pcbs[mpfid];
    if (!pcb->is_created) return E_NOEXS;

    pk_rmpf->fblkcnt = pcb->fblkcnt;
    pk_rmpf->wtskid = TSK_SELF;

    for (ID id = 1; id <= UITRON_MAX_TSKID; ++id) {
        TCB *tcb = &g_kcb.tcbs[id];
        if (tcb->is_created && (tcb->tskstat == TTS_WAI || tcb->tskstat == TTS_WAS)) {
            if (tcb->tskwait == TTW_MPF && tcb->wid == mpfid) {
                pk_rmpf->wtskid = id;
                break;
            }
        }
    }
    return E_OK;
}
