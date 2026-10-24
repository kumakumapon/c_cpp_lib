#include "embed_motion/motion_profile.h"
#include <math.h>
#include <string.h>

#define EPSILON 0.00001f

/* ---------------------------------------------------------
 * ジャーク制限付き S 字プロファイルの部品
 *
 * 速度 0 から dv まで増やす「加速ステージ」を、加速度上限 a・ジャーク上限 j で実現する。
 *   区間1: ジャーク +j で加速度が a_pk まで上昇 (時間 tj)
 *   区間2: 加速度 a_pk 一定 (時間 ta、a_pk に届かない場合は 0)
 *   区間3: ジャーク -j で加速度が 0 まで低下 (時間 tj)
 * 減速ステージは、加速ステージを時間反転したものである。
 * --------------------------------------------------------- */
typedef struct {
    float tj;  /* ジャーク区間の時間 */
    float ta;  /* 等加速度区間の時間 */
    float T;   /* ステージ時間 */
    float d;   /* ステージ中の移動距離 */
} scurve_stage_t;

static scurve_stage_t scurve_stage(float dv, float a, float j) {
    scurve_stage_t st;
    if (dv >= (a * a) / j) {
        /* 加速度上限 a に到達する */
        st.tj = a / j;
        st.ta = dv / a - a / j;
    } else {
        /* 到達しない: 加速度のピークは √(dv·j) */
        st.tj = sqrtf(dv / j);
        st.ta = 0.0f;
    }
    st.T = 2.0f * st.tj + st.ta;
    st.d = 0.5f * dv * st.T; /* 速度は対称に 0 → dv へ変化するので、平均速度は dv/2 */
    return st;
}

/* 加速ステージの時刻 tau (0..T) における 移動距離 / 速度 / 加速度 / ジャーク */
static void scurve_stage_eval(const scurve_stage_t *st, float j, float tau,
                              float *s, float *v, float *a, float *jk) {
    const float tj = st->tj;
    const float ta = st->ta;
    const float a_pk = j * tj;
    const float v1 = 0.5f * j * tj * tj;
    const float s1 = j * tj * tj * tj / 6.0f;

    if (tau <= 0.0f) {
        *s = 0.0f; *v = 0.0f; *a = 0.0f; *jk = j;
    } else if (tau < tj) {
        *jk = j;
        *a = j * tau;
        *v = 0.5f * j * tau * tau;
        *s = j * tau * tau * tau / 6.0f;
    } else if (tau < tj + ta) {
        const float t = tau - tj;
        *jk = 0.0f;
        *a = a_pk;
        *v = v1 + a_pk * t;
        *s = s1 + v1 * t + 0.5f * a_pk * t * t;
    } else {
        float t = tau - tj - ta;
        if (t > tj) t = tj;
        const float v2 = v1 + a_pk * ta;
        const float s2 = s1 + v1 * ta + 0.5f * a_pk * ta * ta;
        *jk = -j;
        *a = a_pk - j * t;
        *v = v2 + a_pk * t - 0.5f * j * t * t;
        *s = s2 + v2 * t + 0.5f * a_pk * t * t - j * t * t * t / 6.0f;
    }
}

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
        /* ジャーク制限付き S 字 (加速ステージ + 等速 + 減速ステージ)。
         * 最高速度・加速度・減速度・ジャークの上限を全て守る。 */
        const float j_max = constraints->max_jerk > 0.0f ? constraints->max_jerk : (a_acc * 5.0f);
        prof->jerk_limit = j_max;

        scurve_stage_t acc = scurve_stage(v_max, a_acc, j_max);
        scurve_stage_t dec = scurve_stage(v_max, a_dec, j_max);
        float v_peak = v_max;

        if (prof->distance >= acc.d + dec.d) {
            /* 最高速度に到達し、残りを等速で進む */
            prof->t_const = (prof->distance - (acc.d + dec.d)) / v_max;
        } else {
            /* 最高速度に届かない: 加減速ステージの距離の合計が移動距離に等しくなる速度を求める
             * (合計距離は速度に対して単調増加なので二分法で解く) */
            float lo = 0.0f, hi = v_max;
            for (int i = 0; i < 60; ++i) {
                const float mid = 0.5f * (lo + hi);
                const float sum = scurve_stage(mid, a_acc, j_max).d + scurve_stage(mid, a_dec, j_max).d;
                if (sum < prof->distance) lo = mid; else hi = mid;
            }
            v_peak = 0.5f * (lo + hi);
            acc = scurve_stage(v_peak, a_acc, j_max);
            dec = scurve_stage(v_peak, a_dec, j_max);
            prof->t_const = 0.0f;
        }
        prof->v_peak = v_peak;
        prof->t_acc = acc.T;
        prof->t_dec = dec.T;
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
    float jk = 0.0f; // 相対ジャーク

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
        /* ジャーク制限付き S 字 */
        const float jl = prof->jerk_limit;
        if (t <= t1) {
            /* 加速ステージ */
            const scurve_stage_t acc = scurve_stage(v_peak, a_acc, jl);
            scurve_stage_eval(&acc, jl, t, &s, &v, &a, &jk);
        } else if (t <= t2) {
            /* 等速 */
            const scurve_stage_t acc = scurve_stage(v_peak, a_acc, jl);
            s = acc.d + v_peak * (t - t1);
            v = v_peak;
        } else {
            /* 減速ステージ: 加速ステージを時間反転したもの */
            const scurve_stage_t acc = scurve_stage(v_peak, a_acc, jl);
            const scurve_stage_t dec = scurve_stage(v_peak, a_dec, jl);
            const float u = t - t2;
            float rs = 0.0f, rv = 0.0f, ra = 0.0f, rj = 0.0f;
            scurve_stage_eval(&dec, jl, dec.T - u, &rs, &rv, &ra, &rj);
            s = acc.d + v_peak * prof->t_const + dec.d - rs;
            v = rv;
            a = -ra;
            jk = rj;
        }
        if (s > prof->distance) s = prof->distance; /* 浮動小数点の丸めで目標を超えない */
    }

    state->position = prof->start_pos + (prof->direction * s);
    state->velocity = prof->direction * v;
    state->acceleration = prof->direction * a;
    state->jerk = prof->direction * jk;
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
