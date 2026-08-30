#include <iostream>
#include <cassert>
#include <cmath>

#include "embed_filter/filter_types.h"
#include "embed_filter/embed_filter.h"
#include "embed_filter/embed_filter.hpp"

void test_kalman1d() {
    std::cout << "[Test] Running 1D Kalman Filter tests..." << std::endl;
    // 真値 25.0、測定ノイズあり
    embed_filter::Kalman1D kf(20.0f, 1.0f, 0.01f, 0.5f);

    float est = 0.0f;
    for (int i = 0; i < 50; ++i) {
        float noisy_meas = 25.0f + ((i % 2 == 0) ? 1.0f : -1.0f);
        est = kf.update(noisy_meas);
    }

    assert(fabsf(est - 25.0f) < 0.5f);
    assert(kf.covariance() < 0.2f);

    std::cout << "[Test] 1D Kalman Filter passed! (Estimated: " << est << ")" << std::endl;
}

void test_median_filter() {
    std::cout << "[Test] Running Median Filter tests..." << std::endl;
    embed_filter::MedianFilter mf(5);

    // 10.0 の連続信号に突発スパイク 100.0 が混入
    mf.update(10.0f);
    mf.update(10.0f);
    mf.update(100.0f); // スパイク
    mf.update(10.0f);
    float out = mf.update(10.0f);

    assert(fabsf(out - 10.0f) < 0.001f); // 100.0 が除去されて 10.0 が維持される

    std::cout << "[Test] Median Filter passed!" << std::endl;
}

void test_low_pass_filter() {
    std::cout << "[Test] Running Low-Pass Filter tests..." << std::endl;
    embed_filter::LowPassFilter lpf(0.2f);

    float val = lpf.update(0.0f);
    assert(val == 0.0f);

    for (int i = 0; i < 20; ++i) {
        val = lpf.update(100.0f);
    }

    assert(val > 95.0f);

    std::cout << "[Test] Low-Pass Filter passed!" << std::endl;
}

void test_imu_filter() {
    std::cout << "[Test] Running IMU Madgwick Filter tests..." << std::endl;
    embed_filter::ImuFilter imu(100.0f, 0.1f);

    // 静止状態 (重力加速度 Z = +1.0g = 9.8m/s^2, ジャイロ 0)
    for (int i = 0; i < 100; ++i) {
        imu.update(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 9.8f);
    }

    euler_angles_t ea = imu.euler_angles();
    // 水平静止なので Roll, Pitch はほぼ 0 rad
    assert(fabsf(ea.roll) < 0.05f);
    assert(fabsf(ea.pitch) < 0.05f);

    std::cout << "[Test] IMU Filter passed! (Roll: " << ea.roll << " rad, Pitch: " << ea.pitch << " rad)" << std::endl;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << " Running embed_filter test suite..." << std::endl;
    std::cout << "========================================" << std::endl;

    test_kalman1d();
    test_median_filter();
    test_low_pass_filter();
    test_imu_filter();

    std::cout << "========================================" << std::endl;
    std::cout << " All embed_filter tests PASSED!" << std::endl;
    std::cout << "========================================" << std::endl;
    return 0;
}
