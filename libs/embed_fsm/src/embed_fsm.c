#include "embed_fsm/embed_fsm.h"
#include <string.h>

void fsm_init(fsm_machine_t *fsm, void *user_data) {
    if (!fsm) return;
    memset(fsm, 0, sizeof(fsm_machine_t));
    fsm->current_state = FSM_STATE_NONE;
    fsm->target_state = FSM_STATE_NONE;
    fsm->user_data = user_data;
}

static fsm_state_t* find_state(fsm_machine_t *fsm, fsm_state_id_t id) {
    if (!fsm || id == FSM_STATE_NONE) return NULL;
    for (size_t i = 0; i < fsm->state_count; ++i) {
        if (fsm->states[i].is_used && fsm->states[i].id == id) {
            return &fsm->states[i];
        }
    }
    return NULL;
}

bool fsm_add_state(fsm_machine_t *fsm, fsm_state_id_t id, fsm_state_id_t parent_id,
                   const char *name, fsm_action_fn on_enter, fsm_action_fn on_exit, fsm_event_fn on_event) {
    if (!fsm || id == FSM_STATE_NONE) return false;
    if (fsm->state_count >= FSM_MAX_STATES) return false;

    // 既存チェック
    if (find_state(fsm, id) != NULL) return false;

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
static size_t get_path_to_root(fsm_machine_t *fsm, fsm_state_id_t id, fsm_state_id_t path[FSM_MAX_STATES]) {
    size_t count = 0;
    fsm_state_id_t cur = id;

    while (cur != FSM_STATE_NONE && count < FSM_MAX_STATES) {
        path[count++] = cur;
        fsm_state_t *st = find_state(fsm, cur);
        if (!st) break;
        cur = st->parent_id;
    }

    return count;
}

bool fsm_transition_to(fsm_machine_t *fsm, fsm_state_id_t target_state) {
    if (!fsm || target_state == FSM_STATE_NONE) return false;
    fsm_state_t *target = find_state(fsm, target_state);
    if (!target) return false;

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
                goto found_lca;
            }
        }
    }

found_lca:
    // 1. 現在状態から LCA 直前まで on_exit を実行
    int exit_limit = (lca_cur_idx >= 0) ? lca_cur_idx : (int)cur_depth;
    for (int i = 0; i < exit_limit; ++i) {
        fsm_state_t *st = find_state(fsm, cur_path[i]);
        if (st && st->on_exit) {
            st->on_exit(fsm, fsm->user_data);
        }
    }

    // 2. LCA 直下から target 状態まで on_enter を実行
    int enter_start = (lca_tgt_idx >= 0) ? (lca_tgt_idx - 1) : ((int)tgt_depth - 1);
    for (int i = enter_start; i >= 0; --i) {
        fsm_state_t *st = find_state(fsm, tgt_path[i]);
        if (st && st->on_enter) {
            st->on_enter(fsm, fsm->user_data);
        }
    }

    fsm->current_state = target_state;
    return true;
}

bool fsm_start(fsm_machine_t *fsm, fsm_state_id_t initial_state) {
    if (!fsm) return false;
    fsm->current_state = FSM_STATE_NONE;
    return fsm_transition_to(fsm, initial_state);
}

bool fsm_dispatch(fsm_machine_t *fsm, const fsm_event_t *event) {
    if (!fsm || !event || fsm->current_state == FSM_STATE_NONE) return false;

    fsm_state_id_t cur_id = fsm->current_state;

    // 階層を上位に向かってバブルアップ
    while (cur_id != FSM_STATE_NONE) {
        fsm_state_t *st = find_state(fsm, cur_id);
        if (!st) break;

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
    if (!fsm || state_id == FSM_STATE_NONE) return false;

    fsm_state_id_t cur = fsm->current_state;
    while (cur != FSM_STATE_NONE) {
        if (cur == state_id) return true;
        fsm_state_t *st = find_state((fsm_machine_t *)fsm, cur);
        if (!st) break;
        cur = st->parent_id;
    }
    return false;
}
