#include "embed_pid/pid_controller.h"
#include <string.h>
#include <math.h>

#define PID_CLAMP(val, min_val, max_val) \
    ((val) < (min_val) ? (min_val) : ((val) > (max_val) ? (max_val) : (val)))

void pid_get_default_config(pid_config_t *config) {
    if (!config) return;
    memset(config, 0, sizeof(pid_config_t));
    config->kp = 1.0f;
    config->ki = 0.0f;
    config->kd = 0.0f;
    config->dt = 0.01f; // 10ms
    config->out_min = -100.0f;
    config->out_max = 100.0f;
    config->deadband = 0.0f;
    config->tau_d = 0.0f; // フィルタ無効
    config->kt = 0.0f;
    config->mode = PID_MODE_POSITIONAL;
    config->anti_windup = PID_ANTI_WINDUP_CLAMPING;
}

void pid_init(embed_pid_t *pid, const pid_config_t *config) {
    if (!pid) return;
    memset(pid, 0, sizeof(embed_pid_t));
    if (config) {
        pid->config = *config;
    } else {
        pid_get_default_config(&pid->config);
    }
    pid->is_auto = true;
    pid_reset(pid);
}

void pid_reset(embed_pid_t *pid) {
    if (!pid) return;
    pid->integral = 0.0f;
    pid->prev_error = 0.0f;
    pid->prev2_error = 0.0f;
    pid->prev_pv = 0.0f;
    pid->prev_d_term = 0.0f;
    pid->prev_output = 0.0f;
    pid->p_term = 0.0f;
    pid->i_term = 0.0f;
    pid->d_term = 0.0f;
    pid->output = 0.0f;
    pid->saturated_output = 0.0f;
}

void pid_set_config(embed_pid_t *pid, const pid_config_t *config) {
    if (pid && config) {
        pid->config = *config;
    }
}

void pid_set_manual_output(embed_pid_t *pid, float manual_output) {
    if (!pid) return;
    pid->saturated_output = PID_CLAMP(manual_output, pid->config.out_min, pid->config.out_max);
    pid->output = pid->saturated_output;
    pid->prev_output = pid->saturated_output;
}

void pid_set_mode(embed_pid_t *pid, bool is_auto, float current_pv) {
    if (!pid) return;
    if (is_auto && !pid->is_auto) {
        /* 手動 -> 自動 切り替え (バプレス転送: Bumpless Transfer)
         * 現在の出力値に合わせて積分項を初期化し、切り替え時のショックをゼロにする
         */
        pid->prev_pv = current_pv;
        pid->prev_error = 0.0f;
        pid->prev2_error = 0.0f;
        pid->prev_d_term = 0.0f;
        
        /* 積分項初期化: output = P + I + D において D=0, P=Kp*e として I = output - P */
        float p_val = pid->config.kp * (0.0f);
        pid->integral = pid->saturated_output - p_val;
    }
    pid->is_auto = is_auto;
}

float pid_compute(embed_pid_t *pid, float setpoint, float process_variable) {
    if (!pid) return 0.0f;

    /* 手動モード時は固定出力を返す */
    if (!pid->is_auto) {
        return pid->saturated_output;
    }

    const pid_config_t *cfg = &pid->config;
    float dt = cfg->dt > 0.000001f ? cfg->dt : 0.001f;

    /* 1. 偏差計算と不感帯 (Deadband) 処理 */
    float error = setpoint - process_variable;
    if (cfg->deadband > 0.0f && fabsf(error) <= cfg->deadband) {
        error = 0.0f;
    }

    /* ---------------------------------------------------------
     * 速度型 / 増分型 PID (Velocity / Incremental PID)
     * --------------------------------------------------------- */
    if (cfg->mode == PID_MODE_VELOCITY) {
        float delta_e1 = error - pid->prev_error;
        float delta_e2 = error - 2.0f * pid->prev_error + pid->prev2_error;

        pid->p_term = cfg->kp * delta_e1;
        pid->i_term = cfg->ki * error * dt;
        pid->d_term = (cfg->kd / dt) * delta_e2;

        float delta_u = pid->p_term + pid->i_term + pid->d_term;
        pid->output = pid->prev_output + delta_u;
        pid->saturated_output = PID_CLAMP(pid->output, cfg->out_min, cfg->out_max);

        /* 状態更新 */
        pid->prev2_error = pid->prev_error;
        pid->prev_error = error;
        pid->prev_output = pid->saturated_output;
        return pid->saturated_output;
    }

    /* ---------------------------------------------------------
     * 位置型 PID (Positional PID) & 微分先行型 (PI-D / I-PD)
     * --------------------------------------------------------- */

    /* 2. 比例項 (P) */
    pid->p_term = cfg->kp * error;

    /* 3. 微分項 (D) */
    float d_raw = 0.0f;
    if (cfg->mode == PID_MODE_DERIVATIVE_ON_PV) {
        /* 微分先行型: 測定値 (PV) の変化率を用いる (-d(PV)/dt) */
        d_raw = -cfg->kd * (process_variable - pid->prev_pv) / dt;
    } else {
        /* 通常型: 偏差 (Error) の変化率を用いる (de/dt) */
        d_raw = cfg->kd * (error - pid->prev_error) / dt;
    }

    /* 微分1次ローパスフィルタ適用: D_filtered = (tau_d * D_prev + dt * D_raw) / (tau_d + dt) */
    if (cfg->tau_d > 0.0f) {
        pid->d_term = (cfg->tau_d * pid->prev_d_term + dt * d_raw) / (cfg->tau_d + dt);
    } else {
        pid->d_term = d_raw;
    }
    pid->prev_d_term = pid->d_term;

    /* 4. 積分項 (I) の仮更新 */
    float i_candidate = pid->integral + (cfg->ki * error * dt);

    /* 5. 制御出力 (MV) の計算 */
    pid->output = pid->p_term + i_candidate + pid->d_term;
    pid->saturated_output = PID_CLAMP(pid->output, cfg->out_min, cfg->out_max);

    /* 6. アンチワインドアップ (Anti-Windup) */
    if (cfg->anti_windup == PID_ANTI_WINDUP_CLAMPING) {
        /* クランプ方式: 飽和かつ同方向の偏差増大時は積分蓄積を停止 */
        bool saturated_high = (pid->output >= cfg->out_max && error > 0.0f);
        bool saturated_low  = (pid->output <= cfg->out_min && error < 0.0f);
        if (!saturated_high && !saturated_low) {
            pid->integral = i_candidate;
        }
    } else if (cfg->anti_windup == PID_ANTI_WINDUP_BACK_CALC) {
        /* バックカリキュレーション方式: 飽和誤差を積分項から引き算 */
        float kt = cfg->kt > 0.0f ? cfg->kt : (1.0f / dt);
        float tracking_diff = pid->saturated_output - pid->output;
        pid->integral += (cfg->ki * error + kt * tracking_diff) * dt;
    } else {
        pid->integral = i_candidate;
    }

    pid->i_term = pid->integral;

    /* 状態更新 */
    pid->prev_error = error;
    pid->prev_pv = process_variable;
    pid->prev_output = pid->saturated_output;

    return pid->saturated_output;
}

void pid_tune_ziegler_nichols(float ku, float tu, zn_tuning_rule_t rule,
                              float *kp_out, float *ki_out, float *kd_out) {
    if (ku <= 0.0f || tu <= 0.0f) {
        if (kp_out) *kp_out = 0.0f;
        if (ki_out) *ki_out = 0.0f;
        if (kd_out) *kd_out = 0.0f;
        return;
    }

    float kp = 0.0f;
    float ti = 0.0f;
    float td = 0.0f;

    switch (rule) {
    case ZN_TUNING_P_ONLY:
        kp = 0.5f * ku;
        ti = 0.0f;
        td = 0.0f;
        break;
    case ZN_TUNING_PI:
        kp = 0.45f * ku;
        ti = tu / 1.2f;
        td = 0.0f;
        break;
    case ZN_TUNING_PID_CLASSIC:
        kp = 0.6f * ku;
        ti = tu / 2.0f;
        td = tu / 8.0f;
        break;
    case ZN_TUNING_PID_SOME_OVERSHOOT:
        kp = 0.33f * ku;
        ti = tu / 2.0f;
        td = tu / 3.0f;
        break;
    case ZN_TUNING_PID_NO_OVERSHOOT:
        kp = 0.2f * ku;
        ti = tu / 2.0f;
        td = tu / 3.0f;
        break;
    }

    if (kp_out) *kp_out = kp;
    if (ki_out) *ki_out = (ti > 0.0f) ? (kp / ti) : 0.0f;
    if (kd_out) *kd_out = kp * td;
}
