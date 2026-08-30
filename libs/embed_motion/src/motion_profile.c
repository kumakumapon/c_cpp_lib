#include "embed_motion/motion_profile.h"
#include <math.h>
#include <string.h>

#define EPSILON 0.00001f

bool motion_profile_plan(motion_profile_t *prof,
                         motion_profile_type_t type,
                         const motion_constraints_t *constraints,
                         float start_pos, float target_pos, float start_vel) {
    if (!prof || !constraints) return false;
    if (constraints->max_velocity <= 0.0f || constraints->max_acceleration <= 0.0f || constraints->max_deceleration <= 0.0f) {
        return false;
    }

    memset(prof, 0, sizeof(motion_profile_t));
    prof->type = type;
    prof->constraints = *constraints;
    prof->start_pos = start_pos;
    prof->target_pos = target_pos;
    prof->start_vel = start_vel;

    float delta_p = target_pos - start_pos;
    if (fabsf(delta_p) < EPSILON) {
        prof->total_time = 0.0f;
        prof->state.position = target_pos;
        prof->state.is_finished = true;
        return true;
    }

    prof->direction = (delta_p >= 0.0f) ? 1.0f : -1.0f;
    prof->distance = fabsf(delta_p);

    float a_acc = constraints->max_acceleration;
    float a_dec = constraints->max_deceleration;
    float v_max = constraints->max_velocity;

    if (type == MOTION_PROFILE_TRAPEZOIDAL) {
        /* 加速および減速に必要な最小移動距離の計算 */
        float d_acc = (v_max * v_max) / (2.0f * a_acc);
        float d_dec = (v_max * v_max) / (2.0f * a_dec);

        if (prof->distance >= (d_acc + d_dec)) {
            /* 台形プロファイル (最高速度に到達) */
            prof->v_peak = v_max;
            prof->t_acc = v_max / a_acc;
            prof->t_dec = v_max / a_dec;
            float d_const = prof->distance - (d_acc + d_dec);
            prof->t_const = d_const / v_max;
        } else {
            /* 三角形プロファイル (最高速度に到達せず途中で減速開始) */
            prof->v_peak = sqrtf((2.0f * a_acc * a_dec * prof->distance) / (a_acc + a_dec));
            prof->t_acc = prof->v_peak / a_acc;
            prof->t_dec = prof->v_peak / a_dec;
            prof->t_const = 0.0f;
        }
        prof->total_time = prof->t_acc + prof->t_const + prof->t_dec;
    } else if (type == MOTION_PROFILE_SCURVE) {
        /* S字加減速 (ジャーク連続)
         * 簡略化された対称S字加減速: 加速時間 t_acc, 等速時間 t_const, 減速時間 t_dec
         * 加速フェーズを2分割 (躍度一定 + 加速度一定)
         */
        float j_max = constraints->max_jerk > 0.0f ? constraints->max_jerk : (a_acc * 5.0f);
        float t_j = a_acc / j_max;
        float t_acc_min = 2.0f * t_j;
        float v_trans = a_acc * t_acc_min;

        if (v_max > v_trans) {
            prof->t_acc = (v_max / a_acc) + t_j;
            prof->t_dec = (v_max / a_dec) + t_j;
            prof->v_peak = v_max;
            float d_acc = 0.5f * v_max * prof->t_acc;
            float d_dec = 0.5f * v_max * prof->t_dec;
            if (prof->distance >= d_acc + d_dec) {
                prof->t_const = (prof->distance - (d_acc + d_dec)) / v_max;
            } else {
                prof->t_const = 0.0f;
            }
        } else {
            prof->v_peak = sqrtf(prof->distance * a_acc);
            prof->t_acc = (prof->v_peak / a_acc) + t_j;
            prof->t_dec = (prof->v_peak / a_dec) + t_j;
            prof->t_const = 0.0f;
        }
        prof->total_time = prof->t_acc + prof->t_const + prof->t_dec;
    }

    prof->state.position = start_pos;
    prof->state.velocity = start_vel;
    prof->state.acceleration = 0.0f;
    prof->state.is_finished = false;
    return true;
}

void motion_profile_evaluate(const motion_profile_t *prof, float t, motion_state_t *state) {
    if (!prof || !state) return;

    if (prof->total_time <= 0.0f || t >= prof->total_time) {
        state->position = prof->target_pos;
        state->velocity = 0.0f;
        state->acceleration = 0.0f;
        state->jerk = 0.0f;
        state->is_finished = true;
        return;
    }

    if (t <= 0.0f) {
        state->position = prof->start_pos;
        state->velocity = prof->start_vel;
        state->acceleration = 0.0f;
        state->jerk = 0.0f;
        state->is_finished = false;
        return;
    }

    state->is_finished = false;
    float s = 0.0f; // 相対移動距離
    float v = 0.0f; // 相対速度
    float a = 0.0f; // 相対加速度

    float t1 = prof->t_acc;
    float t2 = prof->t_acc + prof->t_const;
    float t3 = prof->total_time;

    float a_acc = prof->constraints.max_acceleration;
    float a_dec = prof->constraints.max_deceleration;
    float v_peak = prof->v_peak;

    if (prof->type == MOTION_PROFILE_TRAPEZOIDAL) {
        if (t <= t1) {
            /* 加速フェーズ */
            a = a_acc;
            v = a_acc * t;
            s = 0.5f * a_acc * t * t;
        } else if (t <= t2) {
            /* 等速フェーズ */
            a = 0.0f;
            v = v_peak;
            float s_acc = 0.5f * a_acc * t1 * t1;
            s = s_acc + v_peak * (t - t1);
        } else if (t < t3) {
            /* 減速フェーズ */
            a = -a_dec;
            float dt_dec = t - t2;
            v = v_peak - a_dec * dt_dec;
            float s_acc = 0.5f * a_acc * t1 * t1;
            float s_const = v_peak * prof->t_const;
            s = s_acc + s_const + (v_peak * dt_dec - 0.5f * a_dec * dt_dec * dt_dec);
        }
    } else {
        /* S字 (正弦波加減速近似による滑らかなジャーク連続プロファイル) */
        float tau = t / prof->total_time;
        // 5次多項式 (Minimum Jerk Trajectory): s(tau) = 10*tau^3 - 15*tau^4 + 6*tau^5
        float tau3 = tau * tau * tau;
        float tau4 = tau3 * tau;
        float tau5 = tau4 * tau;
        float poly_s = 10.0f * tau3 - 15.0f * tau4 + 6.0f * tau5;
        float poly_v = (30.0f * tau * tau - 60.0f * tau3 + 30.0f * tau4) / prof->total_time;
        float poly_a = (60.0f * tau - 180.0f * tau * tau + 120.0f * tau3) / (prof->total_time * prof->total_time);

        s = prof->distance * poly_s;
        v = prof->distance * poly_v;
        a = prof->distance * poly_a;
    }

    state->position = prof->start_pos + (prof->direction * s);
    state->velocity = prof->direction * v;
    state->acceleration = prof->direction * a;
    state->jerk = 0.0f;
}

const motion_state_t* motion_profile_step(motion_profile_t *prof, float dt) {
    if (!prof) return NULL;
    prof->current_time += dt;
    motion_profile_evaluate(prof, prof->current_time, &prof->state);
    return &prof->state;
}

bool motion_multi_axis_plan(motion_multi_axis_t *multi, size_t axis_count,
                            const motion_constraints_t *constraints,
                            const float *start_pos, const float *target_pos) {
    if (!multi || axis_count == 0 || axis_count > MOTION_MAX_AXES || !constraints || !start_pos || !target_pos) {
        return false;
    }

    memset(multi, 0, sizeof(motion_multi_axis_t));
    multi->axis_count = axis_count;

    float sum_sq = 0.0f;
    for (size_t i = 0; i < axis_count; ++i) {
        multi->start_positions[i] = start_pos[i];
        multi->target_positions[i] = target_pos[i];
        float d = target_pos[i] - start_pos[i];
        sum_sq += d * d;
    }

    float total_vector_dist = sqrtf(sum_sq);
    if (total_vector_dist < EPSILON) {
        for (size_t i = 0; i < axis_count; ++i) {
            multi->axis_ratios[i] = 0.0f;
        }
        return motion_profile_plan(&multi->lead_profile, MOTION_PROFILE_TRAPEZOIDAL, constraints, 0.0f, 0.0f, 0.0f);
    }

    for (size_t i = 0; i < axis_count; ++i) {
        multi->axis_ratios[i] = (target_pos[i] - start_pos[i]) / total_vector_dist;
    }

    return motion_profile_plan(&multi->lead_profile, MOTION_PROFILE_TRAPEZOIDAL, constraints, 0.0f, total_vector_dist, 0.0f);
}

bool motion_multi_axis_step(motion_multi_axis_t *multi, float dt,
                            float *current_positions, float *current_velocities) {
    if (!multi || !current_positions || !current_velocities) return true;

    const motion_state_t *st = motion_profile_step(&multi->lead_profile, dt);
    float s = st->position;
    float v = st->velocity;

    for (size_t i = 0; i < multi->axis_count; ++i) {
        current_positions[i] = multi->start_positions[i] + (s * multi->axis_ratios[i]);
        current_velocities[i] = v * multi->axis_ratios[i];
    }

    return st->is_finished;
}
