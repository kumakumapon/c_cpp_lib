#pragma once
#ifndef PID_TYPES_H
#define PID_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief PID 制御アルゴリズム形式
 */
typedef enum {
    PID_MODE_POSITIONAL        = 0, /**< 位置型 PID (通常制御) */
    PID_MODE_VELOCITY          = 1, /**< 速度型 / 増分型 PID (パルス・アクチュエータ向け) */
    PID_MODE_DERIVATIVE_ON_PV  = 2, /**< 微分先行型 (PI-D / I-PD: 目標値急変時のキック防止) */
} pid_algorithm_mode_t;

/**
 * @brief アンチワインドアップ (Anti-Windup) 方式
 */
typedef enum {
    PID_ANTI_WINDUP_NONE       = 0, /**< なし */
    PID_ANTI_WINDUP_CLAMPING   = 1, /**< 積分クランプ (飽和時に積分停止) */
    PID_ANTI_WINDUP_BACK_CALC  = 2, /**< バックカリキュレーション (追従型積分補正) */
} pid_anti_windup_t;

/**
 * @brief PID 制御パラメータ (ゲイン・リミット・フィルタ)
 */
typedef struct {
    float kp;                   /**< 比例ゲイン (Proportional Gain) */
    float ki;                   /**< 積分ゲイン (Integral Gain) */
    float kd;                   /**< 微分ゲイン (Derivative Gain) */
    float dt;                   /**< 制御周期 [秒] (Sampling Interval) */
    
    /* 制限設定 */
    float out_min;              /**< 出力下限値 (Output Minimum Limit) */
    float out_max;              /**< 出力上限値 (Output Maximum Limit) */
    float deadband;             /**< 不感帯幅 (Deadband: |e| <= deadband の場合 e=0) */
    
    /* 微分フィルタ (1次ローパスフィルタ時定数: 秒, 0でフィルタ無効) */
    float tau_d;                /**< 微分ローパスフィルタ時定数 Tf [s] */
    
    /* バックカリキュレーション トラッキングゲイン (通常 1/Tt または sqrt(Kp*Ki)) */
    float kt;                   /**< トラッキングゲイン */
    
    pid_algorithm_mode_t mode;  /**< 動作モード */
    pid_anti_windup_t anti_windup; /**< アンチワインドアップ方式 */
} pid_config_t;

/**
 * @brief PID 制御器内部状態構造体
 */
typedef struct {
    pid_config_t config;        /**< パラメータ設定 */
    
    /* 内部状態変数 */
    float integral;             /**< 積分項の蓄積値 */
    float prev_error;           /**< 前回の偏差 e[k-1] */
    float prev2_error;          /**< 前々回の偏差 e[k-2] (速度型用) */
    float prev_pv;              /**< 前回の測定値 pv[k-1] (微分先行型用) */
    float prev_d_term;          /**< 前回のフィルタ済み微分項 */
    float prev_output;          /**< 前回の制御出力 u[k-1] (速度型用) */
    
    /* 監視・デバッグ用各項出力値 */
    float p_term;               /**< 最新の比例項出力 */
    float i_term;               /**< 最新の積分項出力 */
    float d_term;               /**< 最新の微分項出力 */
    float output;               /**< 飽和前の制御出力 */
    float saturated_output;     /**< 上下限リミット後の最終制御出力 */
    
    bool is_auto;               /**< 自動制御フラグ (false: 手動マニュアルモード) */
} embed_pid_t;

/**
 * @brief Ziegler-Nichols 限界感度法 チューニングルール
 */
typedef enum {
    ZN_TUNING_P_ONLY           = 0, /**< P 制御 */
    ZN_TUNING_PI               = 1, /**< PI 制御 */
    ZN_TUNING_PID_CLASSIC      = 2, /**< 古典的 PID 制御 */
    ZN_TUNING_PID_SOME_OVERSHOOT = 3, /**< 微小オーバーシュート抑制 PID */
    ZN_TUNING_PID_NO_OVERSHOOT = 4, /**< オーバーシュートなし PID */
} zn_tuning_rule_t;

#ifdef __cplusplus
}
#endif

#endif /* PID_TYPES_H */
