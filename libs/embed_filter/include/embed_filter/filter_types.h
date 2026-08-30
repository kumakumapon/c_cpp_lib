#pragma once
#ifndef FILTER_TYPES_H
#define FILTER_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FILTER_MEDIAN_MAX_WINDOW 15

/**
 * @brief 1次元カルマンフィルタ構造体
 */
typedef struct {
    float x; /**< 推定状態値 */
    float p; /**< 推定誤差共分散 */
    float q; /**< プロセスノイズ共分散 */
    float r; /**< 観測ノイズ共分散 */
    float k; /**< カルマンゲイン */
} kalman1d_t;

/**
 * @brief 3次元ベクトル (加速度 / ジャイロ)
 */
typedef struct {
    float x;
    float y;
    float z;
} vector3f_t;

/**
 * @brief クォータニオン (四元数)
 */
typedef struct {
    float q0; /**< w */
    float q1; /**< x */
    float q2; /**< y */
    float q3; /**< z */
} quaternion_t;

/**
 * @brief オイラー角 [rad または deg]
 */
typedef struct {
    float roll;
    float pitch;
    float yaw;
} euler_angles_t;

/**
 * @brief 6軸 IMU 姿勢推定フィルタ構造体 (Madgwick / 相補フィルタ)
 */
typedef struct {
    quaternion_t q;
    float beta;       /**< フィルタゲイン */
    float sample_freq;/**< サンプリング周波数 [Hz] */
} imu_filter_t;

/**
 * @brief メディアンフィルタ構造体
 */
typedef struct {
    float buffer[FILTER_MEDIAN_MAX_WINDOW];
    size_t window_size;
    size_t head;
    size_t count;
} median_filter_t;

/**
 * @brief 1次 IIR 低域通過フィルタ (LPF)
 */
typedef struct {
    float alpha;     /**< 平滑化係数 (0.0〜1.0) */
    float prev_out;  /**< 前回の出力値 */
    bool initialized;
} lpf_first_order_t;

#ifdef __cplusplus
}
#endif

#endif /* FILTER_TYPES_H */
