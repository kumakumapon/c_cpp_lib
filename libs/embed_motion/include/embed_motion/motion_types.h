#pragma once
#ifndef MOTION_TYPES_H
#define MOTION_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MOTION_MAX_AXES 4

/**
 * @brief プロファイル種別
 */
typedef enum {
    MOTION_PROFILE_TRAPEZOIDAL = 0, /**< 台形加減速プロファイル (定加速度) */
    MOTION_PROFILE_SCURVE      = 1, /**< S字加減速プロファイル (ジャーク制限・躍度連続) */
} motion_profile_type_t;

/**
 * @brief 単軸モーション拘束条件 (制約パラメータ)
 */
typedef struct {
    float max_velocity;     /**< 最高速度 [units/s] (Vmax > 0) */
    float max_acceleration; /**< 最大加速度 [units/s^2] (Amax > 0) */
    float max_deceleration; /**< 最大減速度 [units/s^2] (Dmax > 0) */
    float max_jerk;         /**< 最大ジャーク / 躍度 [units/s^3] (Jmax > 0, S字用) */
} motion_constraints_t;

/**
 * @brief 軌道上の瞬時状態量
 */
typedef struct {
    float position;         /**< 現在位置 [units] */
    float velocity;         /**< 現在速度 [units/s] */
    float acceleration;     /**< 現在加速度 [units/s^2] */
    float jerk;             /**< 現在ジャーク [units/s^3] */
    bool is_finished;       /**< 目標到達フラグ */
} motion_state_t;

/**
 * @brief 単軸プロファイル生成器構造体
 */
typedef struct {
    motion_profile_type_t type;
    motion_constraints_t constraints;

    /* 目標設定 */
    float start_pos;
    float target_pos;
    float start_vel;
    float target_vel;

    /* 計算済み内部タイムフェーズ */
    float t_acc;            /**< 加速フェーズ時間 [s] */
    float t_const;          /**< 等速フェーズ時間 [s] */
    float t_dec;            /**< 減速フェーズ時間 [s] */
    float total_time;       /**< プロファイル総所要時間 [s] */
    float v_peak;           /**< 到達最高速度 [units/s] */
    float direction;        /**< 移動方向 (+1.0 または -1.0) */
    float distance;         /**< 移動距離 (絶対値) */

    float current_time;     /**< 現在時刻 [s] */
    motion_state_t state;   /**< 現在の状態 */
} motion_profile_t;

/**
 * @brief 多軸直線補間 (Linear Interpolation) 構造体
 */
typedef struct {
    size_t axis_count;
    motion_profile_t lead_profile; /**< 合成軌道プロファイル */
    float start_positions[MOTION_MAX_AXES];
    float target_positions[MOTION_MAX_AXES];
    float axis_ratios[MOTION_MAX_AXES]; /**< 各軸の移動比率 */
} motion_multi_axis_t;

#ifdef __cplusplus
}
#endif

#endif /* MOTION_TYPES_H */
