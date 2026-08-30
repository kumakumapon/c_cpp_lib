#pragma once
#ifndef KINEMATICS_TYPES_H
#define KINEMATICS_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 2D 位置・姿勢 (Pose 2D)
 */
typedef struct {
    float x;     /**< X座標 [m] */
    float y;     /**< Y座標 [m] */
    float theta; /**< 方位角 [rad] */
} pose2d_t;

/**
 * @brief 2D 車体速度 (Twist 2D)
 */
typedef struct {
    float vx;    /**< X方向並進速度 (前方) [m/s] */
    float vy;    /**< Y方向並進速度 (横方向、全方向移動用) [m/s] */
    float omega; /**< 旋回角速度 [rad/s] */
} twist2d_t;

/**
 * @brief 差動2輪パラメータ構造体
 */
typedef struct {
    float wheel_radius; /**< 車輪半径 R [m] */
    float track_width;  /**< トレッド幅 (車輪間距離) L [m] */
} diff_drive_params_t;

/**
 * @brief 差動2輪車輪速度
 */
typedef struct {
    float left;  /**< 左輪速度 [rad/s または m/s] */
    float right; /**< 右輪速度 [rad/s または m/s] */
} diff_wheel_speeds_t;

/**
 * @brief 4輪メカナムホイールパラメータ
 */
typedef struct {
    float wheel_radius; /**< 車輪半径 R [m] */
    float track_width;  /**< トレッド幅 Lx [m] */
    float wheel_base;   /**< ホイールベース Ly [m] */
} mecanum_params_t;

/**
 * @brief 4輪速度
 */
typedef struct {
    float fl; /**< 前左 (Front Left) */
    float fr; /**< 前右 (Front Right) */
    float rl; /**< 後左 (Rear Left) */
    float rr; /**< 後右 (Rear Right) */
} mecanum_wheel_speeds_t;

/**
 * @brief 2自由度アームパラメータ
 */
typedef struct {
    float l1; /**< リンク1長 [m] */
    float l2; /**< リンク2長 [m] */
} arm2dof_params_t;

/**
 * @brief 2関節角度
 */
typedef struct {
    float q1; /**< 第1関節角 [rad] */
    float q2; /**< 第2関節角 [rad] */
} arm2dof_joints_t;

#ifdef __cplusplus
}
#endif

#endif /* KINEMATICS_TYPES_H */
