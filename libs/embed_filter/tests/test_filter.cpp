#include "embed_test/embed_test.hpp"
#include <cmath>
#include <iostream>

#include "embed_filter/embed_filter.h"
#include "embed_filter/embed_filter.hpp"
#include "embed_filter/filter_types.h"

TEST_CASE(test_kalman1d) {
    std::cout << "# [Test] Running 1D Kalman Filter tests..." << std::endl;
    // 真値 25.0、測定ノイズあり
    embed_filter::Kalman1D kf(20.0f, 1.0f, 0.01f, 0.5f);

    float est = 0.0f;
    for (int i = 0; i < 50; ++i) {
        float noisy_meas = 25.0f + ((i % 2 == 0) ? 1.0f : -1.0f);
        est = kf.update(noisy_meas);
    }

    CHECK(fabsf(est - 25.0f) < 0.5f);
    CHECK(kf.covariance() < 0.2f);

    std::cout << "# [Test] 1D Kalman Filter passed! (Estimated: " << est << ")" << std::endl;
}

TEST_CASE(test_median_filter) {
    std::cout << "# [Test] Running Median Filter tests..." << std::endl;
    embed_filter::MedianFilter mf(5);

    // 10.0 の連続信号に突発スパイク 100.0 が混入
    mf.update(10.0f);
    mf.update(10.0f);
    mf.update(100.0f); // スパイク
    mf.update(10.0f);
    float out = mf.update(10.0f);

    CHECK(fabsf(out - 10.0f) < 0.001f); // 100.0 が除去されて 10.0 が維持される

    std::cout << "# [Test] Median Filter passed!" << std::endl;
}

TEST_CASE(test_low_pass_filter) {
    std::cout << "# [Test] Running Low-Pass Filter tests..." << std::endl;
    embed_filter::LowPassFilter lpf(0.2f);

    float val = lpf.update(0.0f);
    CHECK(val == 0.0f);

    for (int i = 0; i < 20; ++i) {
        val = lpf.update(100.0f);
    }

    CHECK(val > 95.0f);

    std::cout << "# [Test] Low-Pass Filter passed!" << std::endl;
}

TEST_CASE(test_imu_filter) {
    std::cout << "# [Test] Running IMU Madgwick Filter tests..." << std::endl;
    embed_filter::ImuFilter imu(100.0f, 0.1f);

    // 静止状態 (重力加速度 Z = +1.0g = 9.8m/s^2, ジャイロ 0)
    for (int i = 0; i < 100; ++i) {
        imu.update(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 9.8f);
    }

    euler_angles_t ea = imu.euler_angles();
    // 水平静止なので Roll, Pitch はほぼ 0 rad
    CHECK(fabsf(ea.roll) < 0.05f);
    CHECK(fabsf(ea.pitch) < 0.05f);

    std::cout << "# [Test] IMU Filter passed! (Roll: " << ea.roll << " rad, Pitch: " << ea.pitch
              << " rad)" << std::endl;
}

// ---- Madgwick IMU: ジャイロの積分と重力補正 ---------------------------------
// 姿勢が重力方向と一致している(勾配が 0)状態でも、ジャイロの回転は必ず積分される。
// 以前は勾配が 0 のとき更新全体がスキップされ、水平静止からの回転が一切反映されなかった。
static euler_angles_t run_imu(float gx, float gy, float gz, float ax, float ay, float az,
                              int steps, float beta = 0.1f) {
    embed_filter::ImuFilter imu(100.0f, beta);
    for (int i = 0; i < steps; ++i)
        imu.update(gx, gy, gz, ax, ay, az);
    return imu.euler_angles();
}

TEST_CASE(imu_integrates_yaw_rate_from_level_rest) {
    // 水平静止から 1 rad/s で 1 秒間ヨー回転する(重力は +Z のまま。ヨーは加速度では観測できない)
    euler_angles_t ea = run_imu(0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 9.8f, 100);
    CHECK_NEAR(ea.yaw, 1.0, 0.02);
    CHECK_NEAR(ea.roll, 0.0, 0.01);
    CHECK_NEAR(ea.pitch, 0.0, 0.01);
    // 回転方向は角速度の符号に従う
    ea = run_imu(0.0f, 0.0f, -0.5f, 0.0f, 0.0f, 9.8f, 200);
    CHECK_NEAR(ea.yaw, -1.0, 0.02);
}

TEST_CASE(imu_integrates_roll_and_pitch_rate_without_accel_correction) {
    // 加速度が無い(0)間は、純粋なジャイロ積分
    euler_angles_t ea = run_imu(1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 100);
    CHECK_NEAR(ea.roll, 1.0, 0.02);
    CHECK_NEAR(ea.pitch, 0.0, 0.01);
    ea = run_imu(0.0f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 200);
    CHECK_NEAR(ea.pitch, 1.0, 0.02);
    CHECK_NEAR(ea.roll, 0.0, 0.01);
}

TEST_CASE(imu_gravity_correction_converges_to_tilt) {
    // 重力が X 軸 / Y 軸方向 → ピッチ / ロールが ±90° に収束する(符号は反転対称)
    euler_angles_t px = run_imu(0, 0, 0, 9.8f, 0, 0, 2000);
    euler_angles_t nx = run_imu(0, 0, 0, -9.8f, 0, 0, 2000);
    CHECK_NEAR(std::fabs(px.pitch), 1.5708, 0.05);
    CHECK_NEAR(px.pitch, -nx.pitch, 0.02);
    euler_angles_t py = run_imu(0, 0, 0, 0, 9.8f, 0, 2000);
    euler_angles_t ny = run_imu(0, 0, 0, 0, -9.8f, 0, 2000);
    CHECK_NEAR(std::fabs(py.roll), 1.5708, 0.05);
    CHECK_NEAR(py.roll, -ny.roll, 0.02);
    // 45° 傾いた重力 (y,z 方向に等分) → ロール 45°
    euler_angles_t tilt = run_imu(0, 0, 0, 0, 6.93f, 6.93f, 2000);
    CHECK_NEAR(std::fabs(tilt.roll), 0.7854, 0.03);
    CHECK_NEAR(tilt.pitch, 0.0, 0.03);
}

TEST_CASE(imu_gyro_bias_is_corrected_by_gravity) {
    // 水平静止中のジャイロバイアス (0.05 rad/s) が累積し続けず、重力補正で打ち消される
    euler_angles_t ea = run_imu(0.05f, 0.0f, 0.0f, 0.0f, 0.0f, 9.8f, 3000, 0.5f);
    CHECK(std::fabs(ea.roll) < 0.15f);
}

EMBED_TEST_MAIN()
