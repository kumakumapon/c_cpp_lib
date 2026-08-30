#pragma once
#ifndef FSM_TYPES_H
#define FSM_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FSM_MAX_STATES          32
#define FSM_MAX_TRANSITIONS     64
#define FSM_STATE_NONE          0xFFFF
#define FSM_STATE_NAME_MAX      32

typedef uint16_t fsm_state_id_t;
typedef uint16_t fsm_event_id_t;

/**
 * @brief イベント構造体
 */
typedef struct {
    fsm_event_id_t id;
    uint32_t param;
    void *user_data;
} fsm_event_t;

/**
 * @brief イベントハンドラ戻り値
 */
typedef enum {
    FSM_RET_HANDLED,    /**< イベントを処理完了 */
    FSM_RET_UNHANDLED,  /**< 未処理（親状態へ委譲） */
    FSM_RET_TRANSITION  /**< 状態遷移を実行 */
} fsm_result_t;

struct fsm_machine;
typedef struct fsm_machine fsm_machine_t;

/* コールバック型定義 */
typedef void (*fsm_action_fn)(fsm_machine_t *fsm, void *user_data);
typedef fsm_result_t (*fsm_event_fn)(fsm_machine_t *fsm, const fsm_event_t *event, void *user_data);

/**
 * @brief 状態定義構造体
 */
typedef struct {
    fsm_state_id_t id;
    fsm_state_id_t parent_id; /**< 親状態ID (ルート直下は FSM_STATE_NONE) */
    char name[FSM_STATE_NAME_MAX];
    fsm_action_fn on_enter;
    fsm_action_fn on_exit;
    fsm_event_fn on_event;
    bool is_used;
} fsm_state_t;

/**
 * @brief 状態遷移マシン構造体
 */
struct fsm_machine {
    fsm_state_t states[FSM_MAX_STATES];
    size_t state_count;
    fsm_state_id_t current_state;
    fsm_state_id_t target_state;
    void *user_data;
};

#ifdef __cplusplus
}
#endif

#endif /* FSM_TYPES_H */
