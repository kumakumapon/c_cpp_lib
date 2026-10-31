#include "embed_fsm/embed_fsm.h"
#include <string.h>

void fsm_init(fsm_machine_t *fsm, void *user_data) {
    if (!fsm)
        return;
    memset(fsm, 0, sizeof(fsm_machine_t));
    fsm->current_state = FSM_STATE_NONE;
    fsm->target_state = FSM_STATE_NONE;
    fsm->user_data = user_data;
}

static fsm_state_t *find_state(fsm_machine_t *fsm, fsm_state_id_t id) {
    if (!fsm || id == FSM_STATE_NONE)
        return NULL;
    for (size_t i = 0; i < fsm->state_count; ++i) {
        if (fsm->states[i].is_used && fsm->states[i].id == id) {
            return &fsm->states[i];
        }
    }
    return NULL;
}

bool fsm_add_state(fsm_machine_t *fsm, fsm_state_id_t id, fsm_state_id_t parent_id,
                   const char *name, fsm_action_fn on_enter, fsm_action_fn on_exit,
                   fsm_event_fn on_event) {
    if (!fsm || id == FSM_STATE_NONE || id == parent_id)
        return false;
    if (fsm->state_count >= FSM_MAX_STATES)
        return false;

    // 既存チェック
    if (find_state(fsm, id) != NULL)
        return false;

    fsm_state_t *st = &fsm->states[fsm->state_count++];
    st->id = id;
    st->parent_id = parent_id;
    st->on_enter = on_enter;
    st->on_exit = on_exit;
    st->on_event = on_event;
    st->is_used = true;

    if (name) {
        strncpy(st->name, name, sizeof(st->name) - 1);
        st->name[sizeof(st->name) - 1] = '\0';
    } else {
        st->name[0] = '\0';
    }

    return true;
}

// 祖先パスを取得 (ルートから指定状態までのパス)
static size_t get_path_to_root(fsm_machine_t *fsm, fsm_state_id_t id,
                               fsm_state_id_t path[FSM_MAX_STATES]) {
    size_t count = 0;
    fsm_state_id_t cur = id;

    while (cur != FSM_STATE_NONE && count < FSM_MAX_STATES) {
        path[count++] = cur;
        fsm_state_t *st = find_state(fsm, cur);
        if (!st)
            break;
        cur = st->parent_id;
    }

    return count;
}

bool fsm_transition_to(fsm_machine_t *fsm, fsm_state_id_t target_state) {
    if (!fsm || target_state == FSM_STATE_NONE)
        return false;
    fsm_state_t *target = find_state(fsm, target_state);
    if (!target)
        return false;
    /* on_exit の最中に遷移を重ねると同じ状態を二重に exit してしまうため受け付けない */
    if (fsm->exiting)
        return false;

    fsm_state_id_t cur_path[FSM_MAX_STATES];
    fsm_state_id_t tgt_path[FSM_MAX_STATES];

    size_t cur_depth = get_path_to_root(fsm, fsm->current_state, cur_path);
    size_t tgt_depth = get_path_to_root(fsm, target_state, tgt_path);

    // 共通の先祖 (LCA: Lowest Common Ancestor) を探索
    int lca_cur_idx = -1;
    int lca_tgt_idx = -1;

    for (int i = 0; i < (int)cur_depth; ++i) {
        for (int j = 0; j < (int)tgt_depth; ++j) {
            if (cur_path[i] == tgt_path[j]) {
                lca_cur_idx = i;
                lca_tgt_idx = j;
                /* 自己遷移 (現在 == 遷移先) は、その状態自身を exit して enter し直す。
                 * 共通の先祖を一段上 (親) に取る */
                if (i == 0 && j == 0) {
                    lca_cur_idx = (cur_depth > 1) ? 1 : -1;
                    lca_tgt_idx = (tgt_depth > 1) ? 1 : -1;
                }
                goto found_lca;
            }
        }
    }

found_lca:;
    fsm->target_state = target_state;

    // 1. 現在状態から LCA 直前まで on_exit を実行
    int exit_limit = (lca_cur_idx >= 0) ? lca_cur_idx : (int)cur_depth;
    fsm->exiting = true;
    for (int i = 0; i < exit_limit; ++i) {
        fsm_state_t *st = find_state(fsm, cur_path[i]);
        if (st && st->on_exit) {
            st->on_exit(fsm, fsm->user_data);
        }
    }
    fsm->exiting = false;

    // 2. LCA 直下から target 状態まで on_enter を実行
    int enter_start = (lca_tgt_idx >= 0) ? (lca_tgt_idx - 1) : ((int)tgt_depth - 1);
    for (int i = enter_start; i >= 0; --i) {
        fsm_state_t *st = find_state(fsm, tgt_path[i]);
        if (!st)
            continue;
        /* enter 中のコールバックから再遷移されても、入りかけの状態を起点にできるよう
         * 現在状態を入った分だけ進めておく */
        fsm->current_state = tgt_path[i];
        if (st->on_enter) {
            st->on_enter(fsm, fsm->user_data);
            /* 再遷移で別の遷移が完了していたら、この遷移の残りの enter は行わない */
            if (fsm->target_state != target_state)
                return true;
        }
    }

    fsm->current_state = target_state;
    fsm->target_state = FSM_STATE_NONE;
    return true;
}

bool fsm_start(fsm_machine_t *fsm, fsm_state_id_t initial_state) {
    if (!fsm)
        return false;
    fsm->current_state = FSM_STATE_NONE;
    return fsm_transition_to(fsm, initial_state);
}

bool fsm_dispatch(fsm_machine_t *fsm, const fsm_event_t *event) {
    if (!fsm || !event || fsm->current_state == FSM_STATE_NONE)
        return false;

    fsm_state_id_t cur_id = fsm->current_state;

    // 階層を上位に向かってバブルアップ
    /* 親の指定が循環していても止まるよう、段数を状態数の上限で打ち切る */
    for (size_t depth = 0; cur_id != FSM_STATE_NONE && depth < FSM_MAX_STATES; ++depth) {
        fsm_state_t *st = find_state(fsm, cur_id);
        if (!st)
            break;

        if (st->on_event) {
            fsm_result_t res = st->on_event(fsm, event, fsm->user_data);
            if (res == FSM_RET_HANDLED || res == FSM_RET_TRANSITION) {
                return true;
            }
        }

        cur_id = st->parent_id;
    }

    return false;
}

fsm_state_id_t fsm_get_current_state(const fsm_machine_t *fsm) {
    return fsm ? fsm->current_state : FSM_STATE_NONE;
}

bool fsm_is_in_state(const fsm_machine_t *fsm, fsm_state_id_t state_id) {
    if (!fsm || state_id == FSM_STATE_NONE)
        return false;

    fsm_state_id_t cur = fsm->current_state;
    for (size_t depth = 0; cur != FSM_STATE_NONE && depth < FSM_MAX_STATES; ++depth) {
        if (cur == state_id)
            return true;
        fsm_state_t *st = find_state((fsm_machine_t *)fsm, cur);
        if (!st)
            break;
        cur = st->parent_id;
    }
    return false;
}
