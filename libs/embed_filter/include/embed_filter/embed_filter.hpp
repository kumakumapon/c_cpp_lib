#pragma once
#ifndef EMBED_FILTER_HPP
#define EMBED_FILTER_HPP

#include "filter_types.h"
#include "embed_filter.h"

namespace embed_filter {

class Kalman1D {
public:
    Kalman1D(float initial_x, float initial_p, float q, float r) {
        kalman1d_init(&kf_, initial_x, initial_p, q, r);
    }

    float update(float measurement) {
        return kalman1d_update(&kf_, measurement);
    }

    float state() const { return kf_.x; }
    float covariance() const { return kf_.p; }

private:
    kalman1d_t kf_{};
};

class ImuFilter {
public:
    ImuFilter(float sample_freq_hz = 100.0f, float beta = 0.1f) {
        imu_filter_init(&filter_, sample_freq_hz, beta);
    }

    void update(float gx_rad_s, float gy_rad_s, float gz_rad_s, float ax_m_s2, float ay_m_s2, float az_m_s2) {
        imu_filter_update(&filter_, gx_rad_s, gy_rad_s, gz_rad_s, ax_m_s2, ay_m_s2, az_m_s2);
    }

    euler_angles_t euler_angles() const {
        euler_angles_t ea{};
        imu_filter_get_euler(&filter_, &ea);
        return ea;
    }

    const quaternion_t &quaternion() const { return filter_.q; }

private:
    imu_filter_t filter_{};
};

class MedianFilter {
public:
    explicit MedianFilter(size_t window_size = 5) {
        median_filter_init(&mf_, window_size);
    }

    float update(float sample) {
        return median_filter_update(&mf_, sample);
    }

private:
    median_filter_t mf_{};
};

class LowPassFilter {
public:
    explicit LowPassFilter(float alpha = 0.1f) {
        lpf_init(&lpf_, alpha);
    }

    LowPassFilter(float cutoff_hz, float sample_freq_hz) {
        lpf_init_from_cutoff(&lpf_, cutoff_hz, sample_freq_hz);
    }

    float update(float sample) {
        return lpf_update(&lpf_, sample);
    }

private:
    lpf_first_order_t lpf_{};
};

} // namespace embed_filter

#endif /* EMBED_FILTER_HPP */
