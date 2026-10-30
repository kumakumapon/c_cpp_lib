#pragma once
#ifndef MOTION_PROFILE_H
#define MOTION_PROFILE_H

#include "motion_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 単軸プロファイルを計画・初期化します (台形 / S字)。
 * 
 * @param prof プロファイル構造体
 * @param type プロファイル種別 (TRAPEZOIDAL / SCURVE)
 * @param constraints 拘束条件 (Vmax, Amax, Dmax, Jmax)。S字は 4 つ全ての上限を守る。
 *                    Jmax <= 0 のとき S字は 5 x Amax を使う (台形は Jmax を使わない)
 * @param start_pos 開始位置
 * @param target_pos 目標位置
 * @param start_vel 開始速度 (現在は 0.0 のみ対応。0 以外は拒否されます)
 * @return true 計画成功 (拘束条件が不正、または start_vel != 0 のとき false)
 */
bool motion_profile_plan(motion_profile_t *prof,
                         motion_profile_type_t type,
                         const motion_constraints_t *constraints,
                         float start_pos, float target_pos, float start_vel);

/**
 * @brief 指定した時刻 t における状態量を評価・計算します。
 * 
 * @param prof プロファイル構造体
 * @param t 開始からの経過時間 [秒]
 * @param state 評価結果格納先
 */
void motion_profile_evaluate(const motion_profile_t *prof, float t, motion_state_t *state);

/**
 * @brief 制御周期 dt ごとにプロファイルを1ステップ進めます。
 * 
 * @param prof プロファイル構造体
 * @param dt 制御周期 [秒]
 * @return const motion_state_t* 更新後の最新状態量
 */
const motion_state_t* motion_profile_step(motion_profile_t *prof, float dt);

/**
 * @brief 多軸直線補間 (Multi-axis Linear Interpolation) を計画します。
 * 
 * @param multi 多軸補間構造体
 * @param axis_count 軸数 (最大 MOTION_MAX_AXES)
 * @param constraints 合成ベクトル拘束条件
 * @param start_pos 各軸開始位置配列
 * @param target_pos 各軸目標位置配列
 * @return true 計画成功
 */
bool motion_multi_axis_plan(motion_multi_axis_t *multi, size_t axis_count,
                            const motion_constraints_t *constraints,
                            const float *start_pos, const float *target_pos);

/**
 * @brief 多軸直線補間を1ステップ進め、各軸の位置・速度を取得します。
 * 
 * @param multi 多軸補間構造体
 * @param dt 制御周期 [秒]
 * @param current_positions 各軸の現在位置出力配列
 * @param current_velocities 各軸の現在速度出力配列
 * @return bool 全軸移動完了なら true
 */
bool motion_multi_axis_step(motion_multi_axis_t *multi, float dt,
                            float *current_positions, float *current_velocities);

#ifdef __cplusplus
}
#endif

#endif /* MOTION_PROFILE_H */
