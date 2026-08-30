#pragma once
#ifndef EMBED_FILTER_H
#define EMBED_FILTER_H

#include "filter_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------
 * 1次元カルマンフィルタ
 * --------------------------------------------------------- */

void kalman1d_init(kalman1d_t *kf, float initial_x, float initial_p, float q, float r);
float kalman1d_update(kalman1d_t *kf, float measurement);

/* ---------------------------------------------------------
 * IMU 姿勢推定フィルタ (Madgwick Algorithm)
 * --------------------------------------------------------- */

void imu_filter_init(imu_filter_t *filter, float sample_freq, float beta);
void imu_filter_update(imu_filter_t *filter, float gx, float gy, float gz, float ax, float ay, float az);
void imu_filter_get_euler(const imu_filter_t *filter, euler_angles_t *euler);

/* ---------------------------------------------------------
 * メディアンフィルタ (スパイクノイズ除去)
 * --------------------------------------------------------- */

void median_filter_init(median_filter_t *mf, size_t window_size);
float median_filter_update(median_filter_t *mf, float sample);

/* ---------------------------------------------------------
 * 1次 IIR 低域通過フィルタ (LPF)
 * --------------------------------------------------------- */

void lpf_init(lpf_first_order_t *lpf, float alpha);
void lpf_init_from_cutoff(lpf_first_order_t *lpf, float cutoff_hz, float sample_freq_hz);
float lpf_update(lpf_first_order_t *lpf, float sample);

#ifdef __cplusplus
}
#endif

#endif /* EMBED_FILTER_H */
