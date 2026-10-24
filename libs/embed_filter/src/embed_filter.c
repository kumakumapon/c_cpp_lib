#include "embed_filter/embed_filter.h"
#include <string.h>
#include <math.h>

#define EPSILON 0.00001f

/* ---------------------------------------------------------
 * 1次元カルマンフィルタ
 * --------------------------------------------------------- */

void kalman1d_init(kalman1d_t *kf, float initial_x, float initial_p, float q, float r) {
    if (!kf) return;
    kf->x = initial_x;
    kf->p = initial_p;
    kf->q = q;
    kf->r = (r > EPSILON) ? r : 0.01f;
    kf->k = 0.0f;
}

float kalman1d_update(kalman1d_t *kf, float measurement) {
    if (!kf) return measurement;

    // 1. 予測ステップ
    kf->p = kf->p + kf->q;

    // 2. 更新ステップ
    kf->k = kf->p / (kf->p + kf->r);
    kf->x = kf->x + kf->k * (measurement - kf->x);
    kf->p = (1.0f - kf->k) * kf->p;

    return kf->x;
}

/* ---------------------------------------------------------
 * IMU 姿勢推定フィルタ (Madgwick)
 * --------------------------------------------------------- */

void imu_filter_init(imu_filter_t *filter, float sample_freq, float beta) {
    if (!filter) return;
    filter->q.q0 = 1.0f;
    filter->q.q1 = 0.0f;
    filter->q.q2 = 0.0f;
    filter->q.q3 = 0.0f;
    filter->sample_freq = (sample_freq > 0.0f) ? sample_freq : 100.0f;
    filter->beta = (beta > 0.0f) ? beta : 0.1f;
}

void imu_filter_update(imu_filter_t *filter, float gx, float gy, float gz, float ax, float ay, float az) {
    if (!filter) return;

    float dt = 1.0f / filter->sample_freq;
    float q0 = filter->q.q0, q1 = filter->q.q1, q2 = filter->q.q2, q3 = filter->q.q3;

    // ジャイロによる角速度からクォータニオン微分を計算 (常に積分する)
    float qDot1 = 0.5f * (-q1 * gx - q2 * gy - q3 * gz);
    float qDot2 = 0.5f * ( q0 * gx + q2 * gz - q3 * gy);
    float qDot3 = 0.5f * ( q0 * gy - q1 * gz + q3 * gx);
    float qDot4 = 0.5f * ( q0 * gz + q1 * gy - q2 * gx);

    // 加速度ベクトルの正規化
    float norm_a = sqrtf(ax * ax + ay * ay + az * az);
    if (norm_a > EPSILON) {
        ax /= norm_a;
        ay /= norm_a;
        az /= norm_a;

        // 勾配降下法による重力方向誤差修正ベクトル (Objective function gradient)
        float _2q0 = 2.0f * q0;
        float _2q1 = 2.0f * q1;
        float _2q2 = 2.0f * q2;
        float _2q3 = 2.0f * q3;
        float _4q0 = 4.0f * q0;
        float _4q1 = 4.0f * q1;
        float _4q2 = 4.0f * q2;
        float _8q1 = 8.0f * q1;
        float _8q2 = 8.0f * q2;
        float q0q0 = q0 * q0;
        float q1q1 = q1 * q1;
        float q2q2 = q2 * q2;
        float q3q3 = q3 * q3;

        float s0 = _4q0 * q2q2 + _2q2 * ax + _4q0 * q1q1 - _2q1 * ay;
        float s1 = _4q1 * q3q3 - _2q3 * ax + 4.0f * q0q0 * q1 - _2q0 * ay - _4q1 + _8q1 * q1q1 + _8q1 * q2q2 + _4q1 * az;
        float s2 = 4.0f * q0q0 * q2 + _2q0 * ax + _4q2 * q3q3 - _2q3 * ay - _4q2 + _8q2 * q1q1 + _8q2 * q2q2 + _4q2 * az;
        float s3 = 4.0f * q1q1 * q3 - _2q1 * ax + 4.0f * q2q2 * q3 - _2q2 * ay;

        // 勾配が 0 (重力方向と姿勢が一致) のときは補正しない。ジャイロの積分は常に適用される。
        float norm_s = sqrtf(s0 * s0 + s1 * s1 + s2 * s2 + s3 * s3);
        if (norm_s > EPSILON) {
            qDot1 -= filter->beta * (s0 / norm_s);
            qDot2 -= filter->beta * (s1 / norm_s);
            qDot3 -= filter->beta * (s2 / norm_s);
            qDot4 -= filter->beta * (s3 / norm_s);
        }
    }

    q0 += qDot1 * dt;
    q1 += qDot2 * dt;
    q2 += qDot3 * dt;
    q3 += qDot4 * dt;

    // クォータニオン正規化
    float norm_q = sqrtf(q0 * q0 + q1 * q1 + q2 * q2 + q3 * q3);
    if (norm_q > EPSILON) {
        filter->q.q0 = q0 / norm_q;
        filter->q.q1 = q1 / norm_q;
        filter->q.q2 = q2 / norm_q;
        filter->q.q3 = q3 / norm_q;
    }
}

void imu_filter_get_euler(const imu_filter_t *filter, euler_angles_t *euler) {
    if (!filter || !euler) return;

    float q0 = filter->q.q0;
    float q1 = filter->q.q1;
    float q2 = filter->q.q2;
    float q3 = filter->q.q3;

    // Roll (X軸回転)
    euler->roll = atan2f(2.0f * (q0 * q1 + q2 * q3), 1.0f - 2.0f * (q1 * q1 + q2 * q2));

    // Pitch (Y軸回転)
    float sinp = 2.0f * (q0 * q2 - q3 * q1);
    if (fabsf(sinp) >= 1.0f) {
        euler->pitch = copysignf(3.14159265f / 2.0f, sinp); // ジンバルロック回避
    } else {
        euler->pitch = asinf(sinp);
    }

    // Yaw (Z軸回転)
    euler->yaw = atan2f(2.0f * (q0 * q3 + q1 * q2), 1.0f - 2.0f * (q2 * q2 + q3 * q3));
}

/* ---------------------------------------------------------
 * メディアンフィルタ
 * --------------------------------------------------------- */

void median_filter_init(median_filter_t *mf, size_t window_size) {
    if (!mf) return;
    memset(mf, 0, sizeof(median_filter_t));
    if (window_size > FILTER_MEDIAN_MAX_WINDOW) window_size = FILTER_MEDIAN_MAX_WINDOW;
    if (window_size % 2 == 0) window_size++; // 奇数化
    if (window_size < 3) window_size = 3;
    mf->window_size = window_size;
}

float median_filter_update(median_filter_t *mf, float sample) {
    if (!mf) return sample;

    mf->buffer[mf->head] = sample;
    mf->head = (mf->head + 1) % mf->window_size;
    if (mf->count < mf->window_size) {
        mf->count++;
    }

    // 作業用バッファにコピーしてソート (挿入ソート)
    float sorted[FILTER_MEDIAN_MAX_WINDOW];
    memcpy(sorted, mf->buffer, mf->count * sizeof(float));

    for (size_t i = 1; i < mf->count; ++i) {
        float key = sorted[i];
        int j = (int)i - 1;
        while (j >= 0 && sorted[j] > key) {
            sorted[j + 1] = sorted[j];
            j--;
        }
        sorted[j + 1] = key;
    }

    return sorted[mf->count / 2];
}

/* ---------------------------------------------------------
 * 1次 IIR 低域通過フィルタ (LPF)
 * --------------------------------------------------------- */

void lpf_init(lpf_first_order_t *lpf, float alpha) {
    if (!lpf) return;
    if (alpha < 0.0f) alpha = 0.0f;
    if (alpha > 1.0f) alpha = 1.0f;
    lpf->alpha = alpha;
    lpf->prev_out = 0.0f;
    lpf->initialized = false;
}

void lpf_init_from_cutoff(lpf_first_order_t *lpf, float cutoff_hz, float sample_freq_hz) {
    if (!lpf || sample_freq_hz <= 0.0f || cutoff_hz <= 0.0f) return;
    float rc = 1.0f / (2.0f * 3.14159265f * cutoff_hz);
    float dt = 1.0f / sample_freq_hz;
    float alpha = dt / (rc + dt);
    lpf_init(lpf, alpha);
}

float lpf_update(lpf_first_order_t *lpf, float sample) {
    if (!lpf) return sample;
    if (!lpf->initialized) {
        lpf->prev_out = sample;
        lpf->initialized = true;
        return sample;
    }

    lpf->prev_out = lpf->prev_out + lpf->alpha * (sample - lpf->prev_out);
    return lpf->prev_out;
}
