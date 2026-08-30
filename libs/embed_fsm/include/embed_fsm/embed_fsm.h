#pragma once
#ifndef EMBED_FSM_H
#define EMBED_FSM_H

#include "fsm_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 階層型状態遷移マシンを初期化します。
 */
void fsm_init(fsm_machine_t *fsm, void *user_data);

/**
 * @brief 状態 (State) を追加登録します。
 */
bool fsm_add_state(fsm_machine_t *fsm, fsm_state_id_t id, fsm_state_id_t parent_id,
                   const char *name, fsm_action_fn on_enter, fsm_action_fn on_exit, fsm_event_fn on_event);

/**
 * @brief 初期状態を設定し、起動 (Entry) 処理を実行します。
 */
bool fsm_start(fsm_machine_t *fsm, fsm_state_id_t initial_state);

/**
 * @brief イベントをディスパッチします。
 * @return true イベントが処理された（または遷移した）、false どの階層でも未処理
 */
bool fsm_dispatch(fsm_machine_t *fsm, const fsm_event_t *event);

/**
 * @brief 状態遷移を要求します (イベントハンドラ内等から呼び出し)。
 */
bool fsm_transition_to(fsm_machine_t *fsm, fsm_state_id_t target_state);

/**
 * @brief 現在の状態IDを取得します。
 */
fsm_state_id_t fsm_get_current_state(const fsm_machine_t *fsm);

/**
 * @brief 指定した状態、またはその子孫状態に現在あるかを判定します。
 */
bool fsm_is_in_state(const fsm_machine_t *fsm, fsm_state_id_t state_id);

#ifdef __cplusplus
}
#endif

#endif /* EMBED_FSM_H */
