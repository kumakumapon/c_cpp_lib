#pragma once
#ifndef PID_CONTROLLER_H
#define PID_CONTROLLER_H

#include "pid_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief デフォルトのPID設定を取得します。
 */
void pid_get_default_config(pid_config_t *config);

/**
 * @brief PIDコントローラを初期化します。
 * 
 * @param pid コントローラ構造体へのポインタ
 * @param config パラメータ設定構造体へのポインタ (NULLでデフォルト設定)
 */
void pid_init(embed_pid_t *pid, const pid_config_t *config);

/**
 * @brief PIDコントローラの内部状態（積分項、過去偏差等）をリセットします。
 */
void pid_reset(embed_pid_t *pid);

/**
 * @brief パラメータ設定を動的に更新します。
 */
void pid_set_config(embed_pid_t *pid, const pid_config_t *config);

/**
 * @brief PID制御計算を1ステップ実行します。
 * 
 * @param pid コントローラ構造体へのポインタ
 * @param setpoint 目標値 (SP: Setpoint)
 * @param process_variable 現在値 / 測定値 (PV: Process Variable)
 * @return float 制御出力値 (MV: Manipulated Variable)
 */
float pid_compute(embed_pid_t *pid, float setpoint, float process_variable);

/**
 * @brief 手動モード (Manual) での出力値を設定します。
 * 
 * @param pid コントローラ構造体へのポインタ
 * @param manual_output 手動操作量
 */
void pid_set_manual_output(embed_pid_t *pid, float manual_output);

/**
 * @brief 自動/手動モードを切り替えます (バプレス転送 / Bumpless Transfer 対応)。
 * 
 * @param pid コントローラ構造体へのポインタ
 * @param is_auto true: 自動 (Auto), false: 手動 (Manual)
 * @param current_pv 現在の測定値 (バプレス初期化用)
 */
void pid_set_mode(embed_pid_t *pid, bool is_auto, float current_pv);

/**
 * @brief Ziegler-Nichols 限界感度法に基づくPIDゲイン自動計算を行います。
 * 
 * @param ku 限界感度ゲイン (Ultimate Gain: 発散直前の振動ゲイン)
 * @param tu 限界周期 (Ultimate Period [秒]: 持続振動の周期)
 * @param rule チューニング規則
 * @param kp_out 計算されたKp格納先
 * @param ki_out 計算されたKi格納先
 * @param kd_out 計算されたKd格納先
 */
void pid_tune_ziegler_nichols(float ku, float tu, zn_tuning_rule_t rule,
                              float *kp_out, float *ki_out, float *kd_out);

#ifdef __cplusplus
}
#endif

#endif /* PID_CONTROLLER_H */
