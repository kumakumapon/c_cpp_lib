// 各フィルタの理論値を固定するテスト。期待値は漸化式の手計算、または幾何学的に決まる値にする。
#include "embed_test/embed_test.hpp"
#include <cmath>
#include <initializer_list>

#include "embed_filter/embed_filter.h"

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kTol = 1e-5;

double norm(const quaternion_t &q) {
    return std::sqrt(double(q.q0) * q.q0 + double(q.q1) * q.q1 + double(q.q2) * q.q2 + double(q.q3) * q.q3);
}
} // namespace

// ---- ローパス ----------------------------------------------------------------

TEST_CASE(lpf_follows_the_recurrence_exactly) {
    // y[n] = y[n-1] + a (x[n] - y[n-1])。最初のサンプルはそのまま出力(初期化)
    lpf_first_order_t f;
    lpf_init(&f, 0.5f);
    CHECK_NEAR(lpf_update(&f, 8.0f), 8.0, kTol);
    CHECK_NEAR(lpf_update(&f, 0.0f), 4.0, kTol);
    CHECK_NEAR(lpf_update(&f, 0.0f), 2.0, kTol);
    CHECK_NEAR(lpf_update(&f, 16.0f), 9.0, kTol);
    CHECK_NEAR(lpf_update(&f, 9.0f), 9.0, kTol); // 入力 = 出力なら変化しない
}

TEST_CASE(lpf_step_response_matches_one_minus_decay_power) {
    // 0 で初期化してから 100 のステップ入力: y[n] = 100 (1 - 0.8^n)。63.2% (1 - 1/e) を超えるのは 5 サンプル目
    lpf_first_order_t f;
    lpf_init(&f, 0.2f);
    lpf_update(&f, 0.0f);
    int first_over = 0;
    for (int n = 1; n <= 30; ++n) {
        const float y = lpf_update(&f, 100.0f);
        CHECK_NEAR(y, 100.0 * (1.0 - std::pow(0.8, n)), 1e-3);
        if (!first_over && y >= 63.2f) first_over = n;
    }
    CHECK_EQ(first_over, 5);
}

TEST_CASE(lpf_alpha_limits_and_clamping) {
    lpf_first_order_t f;
    lpf_init(&f, 0.0f); // 0: 最初の値のまま動かない
    lpf_update(&f, 3.0f);
    CHECK_NEAR(lpf_update(&f, 100.0f), 3.0, kTol);
    lpf_init(&f, 1.0f); // 1: 入力そのまま
    lpf_update(&f, 3.0f);
    CHECK_NEAR(lpf_update(&f, 100.0f), 100.0, kTol);
    lpf_init(&f, -0.5f); // 範囲外は 0..1 に丸める
    CHECK_NEAR(f.alpha, 0.0, kTol);
    lpf_init(&f, 1.5f);
    CHECK_NEAR(f.alpha, 1.0, kTol);
    lpf_init(&f, 0.25f);
    CHECK_NEAR(f.alpha, 0.25, kTol);
}

TEST_CASE(lpf_reset_and_cutoff_design) {
    lpf_first_order_t f;
    lpf_init(&f, 0.5f);
    lpf_update(&f, 10.0f);
    lpf_update(&f, 20.0f);
    lpf_init(&f, 0.5f); // 再初期化すると履歴を捨て、次のサンプルをそのまま出す
    CHECK(!f.initialized);
    CHECK_NEAR(lpf_update(&f, -7.0f), -7.0, kTol);

    // alpha = dt / (RC + dt), RC = 1 / (2 pi fc)
    lpf_init_from_cutoff(&f, 1.0f, 100.0f);
    const double rc = 1.0 / (2.0 * kPi * 1.0);
    CHECK_NEAR(f.alpha, 0.01 / (rc + 0.01), 1e-5);
    CHECK_NEAR(f.alpha, 0.059118, 1e-5);
    lpf_init_from_cutoff(&f, 10.0f, 100.0f); // 遮断周波数が高いほど alpha は大きい
    CHECK_NEAR(f.alpha, 0.385869, 1e-5);

    // 不正な引数では何も変更しない
    f.alpha = 0.7f;
    lpf_init_from_cutoff(&f, 0.0f, 100.0f);
    lpf_init_from_cutoff(&f, -1.0f, 100.0f);
    lpf_init_from_cutoff(&f, 1.0f, 0.0f);
    lpf_init_from_cutoff(nullptr, 1.0f, 100.0f);
    CHECK_NEAR(f.alpha, 0.7, kTol);
    CHECK_NEAR(lpf_update(nullptr, 5.0f), 5.0, kTol);
}

// ---- メディアン --------------------------------------------------------------

TEST_CASE(median_window_size_is_normalized) {
    median_filter_t m;
    const struct { size_t in, out; } cases[] = {{0, 3}, {1, 3}, {2, 3}, {3, 3}, {4, 5}, {5, 5}, {6, 7},
                                                {14, 15}, {15, 15}, {16, 15}, {100, 15}};
    for (const auto &c : cases) {
        median_filter_init(&m, c.in);
        CHECK_EQ(m.window_size, c.out);
        CHECK_EQ(m.count, size_t(0));
    }
    CHECK_NEAR(median_filter_update(nullptr, 4.0f), 4.0, kTol);
}

TEST_CASE(median_before_the_window_fills_uses_the_samples_so_far) {
    median_filter_t m;
    median_filter_init(&m, 5);
    CHECK_NEAR(median_filter_update(&m, 1.0f), 1.0, kTol);  // [1]
    CHECK_NEAR(median_filter_update(&m, 2.0f), 2.0, kTol);  // [1 2]    偶数個は上側の中央値
    CHECK_NEAR(median_filter_update(&m, 3.0f), 2.0, kTol);  // [1 2 3]
    CHECK_NEAR(median_filter_update(&m, 4.0f), 3.0, kTol);  // [1 2 3 4]
    CHECK_NEAR(median_filter_update(&m, 5.0f), 3.0, kTol);  // [1 2 3 4 5]
    CHECK_EQ(m.count, size_t(5));
}

TEST_CASE(median_slides_over_a_ramp_and_a_hand_computed_sequence) {
    median_filter_t m;
    median_filter_init(&m, 5);
    const double ramp[] = {1, 2, 2, 3, 3, 4, 5, 6}; // 窓が満ちた後は直近 5 個の中央値 = n - 2
    for (int i = 0; i < 8; ++i)
        CHECK_NEAR(median_filter_update(&m, float(i + 1)), ramp[i], kTol);

    median_filter_init(&m, 3);
    const float in[] = {1, 5, 3, 7, 2, 9, 4};
    const double want[] = {1, 5, 3, 5, 3, 7, 4}; // 窓: [1] [1 5] [1 5 3] [7 5 3] [7 2 3] [7 2 9] [4 2 9]
    for (int i = 0; i < 7; ++i)
        CHECK_NEAR(median_filter_update(&m, in[i]), want[i], kTol);
}

TEST_CASE(median_removes_spikes_shorter_than_half_the_window) {
    // 窓 5: 連続 2 個のスパイクは除去される。窓 3: 連続 2 個は通り抜ける
    median_filter_t m5, m3;
    median_filter_init(&m5, 5);
    median_filter_init(&m3, 3);
    const float sig[] = {10, 10, 10, 100, 100, 10, 10};
    float o5 = 0, o3 = 0;
    bool m3_passed_spike = false;
    for (float s : sig) {
        o5 = median_filter_update(&m5, s);
        o3 = median_filter_update(&m3, s);
        CHECK_NEAR(o5, 10.0, kTol);
        if (o3 == 100.0f) m3_passed_spike = true;
    }
    CHECK(m3_passed_spike);
    CHECK_NEAR(o3, 10.0, kTol);
}

TEST_CASE(median_maximum_window_boundary) {
    median_filter_t m;
    median_filter_init(&m, 15);
    float out = 0;
    for (int i = 1; i <= 20; ++i)
        out = median_filter_update(&m, float(i));
    CHECK_NEAR(out, 13.0, kTol); // 直近 15 個 (6..20) の中央値
    CHECK_EQ(m.count, size_t(15));
    out = median_filter_update(&m, 21.0f);
    CHECK_NEAR(out, 14.0, kTol); // 7..21
}

// ---- カルマン ----------------------------------------------------------------

TEST_CASE(kalman_matches_the_hand_computed_recurrence) {
    // x0=0, P0=1, Q=0, R=1, 測定値 10 が続く: K_n = P_n = 1/(n+1), x_n = 10 n/(n+1)
    kalman1d_t kf;
    kalman1d_init(&kf, 0.0f, 1.0f, 0.0f, 1.0f);
    const double x[] = {5.0, 20.0 / 3.0, 7.5, 8.0};
    const double k[] = {0.5, 1.0 / 3.0, 0.25, 0.2};
    const double p[] = {0.5, 1.0 / 3.0, 0.25, 0.2};
    for (int i = 0; i < 4; ++i) {
        CHECK_NEAR(kalman1d_update(&kf, 10.0f), x[i], 1e-5);
        CHECK_NEAR(kf.x, x[i], 1e-5);
        CHECK_NEAR(kf.k, k[i], 1e-5);
        CHECK_NEAR(kf.p, p[i], 1e-5);
    }
}

TEST_CASE(kalman_process_noise_is_added_before_the_update) {
    // P0=0, Q=1, R=1: 予測 P=1 → K=0.5 → x=0.5 z, P=0.5。次は予測 P=1.5 → K=0.6
    kalman1d_t kf;
    kalman1d_init(&kf, 0.0f, 0.0f, 1.0f, 1.0f);
    CHECK_NEAR(kalman1d_update(&kf, 8.0f), 4.0, 1e-5);
    CHECK_NEAR(kf.k, 0.5, 1e-5);
    CHECK_NEAR(kf.p, 0.5, 1e-5);
    CHECK_NEAR(kalman1d_update(&kf, 8.0f), 4.0 + 0.6 * 4.0, 1e-5);
    CHECK_NEAR(kf.k, 0.6, 1e-5);
    CHECK_NEAR(kf.p, 0.4 * 1.5, 1e-5);
}

TEST_CASE(kalman_converges_to_the_steady_state_gain) {
    // 定常: P- = (Q + sqrt(Q^2 + 4QR)) / 2, K = P- / (P- + R)
    const double q = 0.01, r = 1.0;
    const double pm = (q + std::sqrt(q * q + 4 * q * r)) / 2;
    kalman1d_t kf;
    kalman1d_init(&kf, 0.0f, 1.0f, float(q), float(r));
    for (int i = 0; i < 500; ++i)
        kalman1d_update(&kf, 3.0f);
    CHECK_NEAR(kf.k, pm / (pm + r), 1e-4);
    CHECK_NEAR(kf.x, 3.0, 1e-3);
}

TEST_CASE(kalman_extreme_noise_settings_and_guards) {
    kalman1d_t kf;
    kalman1d_init(&kf, 0.0f, 1.0f, 0.0f, 1e6f); // 観測がほぼ無視される
    kalman1d_update(&kf, 100.0f);
    CHECK(kf.x < 0.01f);
    kalman1d_init(&kf, 0.0f, 1.0f, 1e6f, 1.0f); // 予測がほぼ無視され、観測に追従する
    kalman1d_update(&kf, 100.0f);
    CHECK_NEAR(kf.x, 100.0, 0.01);

    // R が 0 以下 / ごく小さいときは 0.01 に置き換える: P=0.01 → K = 0.01 / 0.02
    kalman1d_init(&kf, 0.0f, 0.01f, 0.0f, 0.0f);
    CHECK_NEAR(kf.r, 0.01, 1e-7);
    CHECK_NEAR(kalman1d_update(&kf, 10.0f), 5.0, 1e-4);
    kalman1d_init(&kf, 0.0f, 0.0f, 0.0f, -3.0f);
    CHECK_NEAR(kf.r, 0.01, 1e-7);
    kalman1d_init(&kf, 0.0f, 0.0f, 0.0f, 0.5f);
    CHECK_NEAR(kf.r, 0.5, 1e-7);
    CHECK_NEAR(kf.k, 0.0, 1e-7);

    CHECK_NEAR(kalman1d_update(nullptr, 7.5f), 7.5, kTol);
}

// ---- Madgwick ----------------------------------------------------------------

namespace {
quaternion_t axis_angle(double ax, double ay, double az, double angle) {
    const double s = std::sin(angle / 2);
    return {float(std::cos(angle / 2)), float(ax * s), float(ay * s), float(az * s)};
}

euler_angles_t euler_of(const quaternion_t &q) {
    imu_filter_t f;
    imu_filter_init(&f, 100.0f, 0.1f);
    f.q = q;
    euler_angles_t e;
    imu_filter_get_euler(&f, &e);
    return e;
}

// 静止姿勢 (roll, pitch) で加速度計が示す重力ベクトルを与え続けたときの収束先
euler_angles_t settle(double roll, double pitch, int steps, float beta = 0.1f) {
    imu_filter_t f;
    imu_filter_init(&f, 100.0f, beta);
    // 機体座標での重力 = R^T (0,0,1)。R = Ry(pitch) Rx(roll) の ZYX 規約
    const float ax = float(-std::sin(pitch) * 9.8);
    const float ay = float(std::sin(roll) * std::cos(pitch) * 9.8);
    const float az = float(std::cos(roll) * std::cos(pitch) * 9.8);
    for (int i = 0; i < steps; ++i)
        imu_filter_update(&f, 0, 0, 0, ax, ay, az);
    euler_angles_t e;
    imu_filter_get_euler(&f, &e);
    return e;
}
} // namespace

TEST_CASE(imu_init_defaults_and_identity_attitude) {
    imu_filter_t f;
    imu_filter_init(&f, 0.0f, 0.0f);
    CHECK_NEAR(f.sample_freq, 100.0, kTol);
    CHECK_NEAR(f.beta, 0.1, kTol);
    imu_filter_init(&f, 200.0f, 0.4f);
    CHECK_NEAR(f.sample_freq, 200.0, kTol);
    CHECK_NEAR(f.beta, 0.4, kTol);
    CHECK_NEAR(f.q.q0, 1.0, kTol);
    CHECK_NEAR(f.q.q1, 0.0, kTol);
    CHECK_NEAR(f.q.q2, 0.0, kTol);
    CHECK_NEAR(f.q.q3, 0.0, kTol);

    // ジャイロも加速度も 0 なら姿勢は変わらない
    for (int i = 0; i < 10; ++i)
        imu_filter_update(&f, 0, 0, 0, 0, 0, 0);
    CHECK_NEAR(f.q.q0, 1.0, kTol);
    CHECK_NEAR(norm(f.q), 1.0, kTol);

    imu_filter_update(nullptr, 0, 0, 0, 0, 0, 1); // 落ちない
    euler_angles_t e{1, 2, 3};
    imu_filter_get_euler(nullptr, &e);
    imu_filter_get_euler(&f, nullptr);
    CHECK_NEAR(e.roll, 1.0, kTol); // 変更されない
}

TEST_CASE(imu_quaternion_stays_normalized) {
    imu_filter_t f;
    imu_filter_init(&f, 100.0f, 0.2f);
    for (int i = 0; i < 2000; ++i) {
        const float t = float(i) * 0.01f;
        imu_filter_update(&f, 2.0f * std::sin(t), 1.5f * std::cos(1.3f * t), 0.7f, 3.0f * std::sin(0.7f * t),
                          2.0f * std::cos(t), 9.0f + std::sin(2.0f * t));
        CHECK_NEAR(norm(f.q), 1.0, 1e-5);
    }
}

TEST_CASE(imu_gyro_integration_gives_the_axis_angle_quaternion) {
    // 加速度なしの純粋なジャイロ積分: x 軸まわり 1 rad/s で 1 s → q = (cos 0.5, sin 0.5, 0, 0)
    imu_filter_t f;
    imu_filter_init(&f, 1000.0f, 0.1f);
    for (int i = 0; i < 1000; ++i)
        imu_filter_update(&f, 1.0f, 0, 0, 0, 0, 0);
    CHECK_NEAR(f.q.q0, std::cos(0.5), 1e-3);
    CHECK_NEAR(f.q.q1, std::sin(0.5), 1e-3);
    CHECK_NEAR(f.q.q2, 0.0, 1e-4);
    CHECK_NEAR(f.q.q3, 0.0, 1e-4);

    // y 軸 / z 軸、逆回転も同様 (符号規約)
    imu_filter_init(&f, 1000.0f, 0.1f);
    for (int i = 0; i < 1000; ++i)
        imu_filter_update(&f, 0, -1.0f, 0, 0, 0, 0);
    CHECK_NEAR(f.q.q2, -std::sin(0.5), 1e-3);
    imu_filter_init(&f, 1000.0f, 0.1f);
    for (int i = 0; i < 1000; ++i)
        imu_filter_update(&f, 0, 0, 1.0f, 0, 0, 0);
    CHECK_NEAR(f.q.q3, std::sin(0.5), 1e-3);
    CHECK_NEAR(f.q.q1, 0.0, 1e-4);
}

TEST_CASE(imu_euler_conversion_matches_known_quaternions) {
    euler_angles_t e = euler_of(axis_angle(1, 0, 0, kPi / 2));
    CHECK_NEAR(e.roll, kPi / 2, 1e-4);
    CHECK_NEAR(e.pitch, 0.0, 1e-4);
    CHECK_NEAR(e.yaw, 0.0, 1e-4);

    e = euler_of(axis_angle(0, 1, 0, kPi / 6)); // ピッチ 30°
    CHECK_NEAR(e.pitch, kPi / 6, 1e-4);
    CHECK_NEAR(e.roll, 0.0, 1e-4);
    e = euler_of(axis_angle(0, 0, 1, kPi / 3)); // ヨー 60°
    CHECK_NEAR(e.yaw, kPi / 3, 1e-4);
    e = euler_of(axis_angle(0, 0, 1, -kPi / 3));
    CHECK_NEAR(e.yaw, -kPi / 3, 1e-4);

    // ZYX 規約: q = qz(10°) * qy(20°) * qx(30°) のオイラー角は (roll 30°, pitch 20°, yaw 10°)
    const double r = 30 * kPi / 180, p = 20 * kPi / 180, y = 10 * kPi / 180;
    const double cr = std::cos(r / 2), sr = std::sin(r / 2), cp = std::cos(p / 2), sp = std::sin(p / 2),
                 cy = std::cos(y / 2), sy = std::sin(y / 2);
    const quaternion_t q{float(cr * cp * cy + sr * sp * sy), float(sr * cp * cy - cr * sp * sy),
                         float(cr * sp * cy + sr * cp * sy), float(cr * cp * sy - sr * sp * cy)};
    e = euler_of(q);
    CHECK_NEAR(e.roll, r, 1e-4);
    CHECK_NEAR(e.pitch, p, 1e-4);
    CHECK_NEAR(e.yaw, y, 1e-4);
}

TEST_CASE(imu_euler_pitch_is_clamped_at_the_singularity) {
    // ピッチ ±90°。丸めで |sin| が 1 をわずかに超えても NaN にならず ±π/2 になる
    // sin(pitch) = 2 q0 q2 = 1.0082 > 1 (丸め誤差を模した入力) でも NaN にならず ±π/2 に固定される
    euler_angles_t e = euler_of({0.71f, 0.0f, 0.71f, 0.0f});
    CHECK_NEAR(e.pitch, kPi / 2, 1e-6);
    CHECK(!std::isnan(e.roll) && !std::isnan(e.yaw));
    e = euler_of({0.71f, 0.0f, -0.71f, 0.0f});
    CHECK_NEAR(e.pitch, -kPi / 2, 1e-6);
    e = euler_of({0.70710678f, 0.0f, 0.70710678f, 0.0f}); // ちょうど 90° (asin は 1 付近で急峻なので緩め)
    CHECK_NEAR(e.pitch, kPi / 2, 2e-3);
    e = euler_of({0.8f, 0.0f, 0.6f, 0.0f}); // sin(pitch) = 0.96 → ±1 未満は通常の asin
    CHECK_NEAR(e.pitch, std::asin(0.96), 1e-4);
}

TEST_CASE(imu_gravity_correction_converges_to_known_attitudes) {
    // 傾斜 30° / 45° / 90°。符号も含めて合う (ロール: ay>0 で正、ピッチ: ax<0 で正)
    for (double deg : {30.0, 45.0, 90.0}) {
        const double a = deg * kPi / 180;
        euler_angles_t e = settle(a, 0, 3000);
        CHECK_NEAR(e.roll, a, 0.02);
        CHECK_NEAR(e.pitch, 0.0, 0.02);
        e = settle(-a, 0, 3000);
        CHECK_NEAR(e.roll, -a, 0.02);
        e = settle(0, a, 3000);
        CHECK_NEAR(e.pitch, a, 0.02);
        CHECK_NEAR(e.roll, 0.0, 0.03);
        e = settle(0, -a, 3000);
        CHECK_NEAR(e.pitch, -a, 0.02);
    }
    euler_angles_t e = settle(25 * kPi / 180, 35 * kPi / 180, 4000); // 複合姿勢
    CHECK_NEAR(e.roll, 25 * kPi / 180, 0.02);
    CHECK_NEAR(e.pitch, 35 * kPi / 180, 0.02);
}

TEST_CASE(imu_accel_magnitude_does_not_matter_and_beta_sets_the_convergence_speed) {
    // 加速度は正規化されるので、大きさ(単位)によらず同じ結果
    imu_filter_t a, b;
    imu_filter_init(&a, 100.0f, 0.1f);
    imu_filter_init(&b, 100.0f, 0.1f);
    for (int i = 0; i < 100; ++i) {
        imu_filter_update(&a, 0, 0, 0, 0, 0.5f, 0.866f);
        imu_filter_update(&b, 0, 0, 0, 0, 9.8f * 0.5f, 9.8f * 0.866f);
    }
    CHECK_NEAR(a.q.q1, b.q.q1, 1e-5);
    CHECK_NEAR(a.q.q0, b.q.q0, 1e-5);

    // β が大きいほど速く重力方向へ寄る: 30° 傾斜に 0.5 s 後の残差
    const double target = 30 * kPi / 180;
    const double err_slow = std::fabs(settle(target, 0, 50, 0.05f).roll - target);
    const double err_mid = std::fabs(settle(target, 0, 50, 0.1f).roll - target);
    const double err_fast = std::fabs(settle(target, 0, 50, 0.4f).roll - target);
    CHECK(err_fast < err_mid);
    CHECK(err_mid < err_slow);
    CHECK(err_slow < target); // どの β でも正しい向きに進む(悪化しない)
}

TEST_CASE(imu_gyro_and_gravity_are_blended) {
    // 傾いた姿勢で静止(重力 30° ロール)。ジャイロにバイアスがあっても重力補正が打ち消す
    imu_filter_t f;
    imu_filter_init(&f, 100.0f, 0.5f);
    const float ay = 9.8f * std::sin(0.5236f), az = 9.8f * std::cos(0.5236f);
    for (int i = 0; i < 3000; ++i)
        imu_filter_update(&f, 0.0f, 0.05f, 0.0f, 0, ay, az);
    euler_angles_t e;
    imu_filter_get_euler(&f, &e);
    CHECK_NEAR(e.roll, 0.5236, 0.02);
    CHECK_NEAR(e.pitch, 0.0, 0.02);

    // ジャイロが回すあいだは(重力補正に抗して)姿勢が動く: ヨーは重力で観測できないので純粋に積分される
    imu_filter_init(&f, 100.0f, 0.5f);
    for (int i = 0; i < 100; ++i)
        imu_filter_update(&f, 0, 0, 0.5f, 0, 0, 9.8f);
    imu_filter_get_euler(&f, &e);
    CHECK_NEAR(e.yaw, 0.5, 0.01);
}

// ---- Madgwick: 1 ステップを論文の式(目的関数 f とヤコビアン J の J^T f)と照合 ----------------

namespace {
struct Q {
    double w, x, y, z;
};

Q mul(const Q &a, const Q &b) {
    return {a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z, a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x, a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w};
}

// Madgwick (2010) の式 (24)(25)(42)(43): q_dot = 1/2 q (x) (0, w) - beta * grad / |grad|
Q reference_step(Q q, double gx, double gy, double gz, double ax, double ay, double az, double beta, double dt) {
    const double n = std::sqrt(ax * ax + ay * ay + az * az);
    ax /= n, ay /= n, az /= n;
    const double f1 = 2 * (q.x * q.z - q.w * q.y) - ax;
    const double f2 = 2 * (q.w * q.x + q.y * q.z) - ay;
    const double f3 = 2 * (0.5 - q.x * q.x - q.y * q.y) - az;
    // J^T f
    double g[4] = {-2 * q.y * f1 + 2 * q.x * f2, 2 * q.z * f1 + 2 * q.w * f2 - 4 * q.x * f3,
                   -2 * q.w * f1 + 2 * q.z * f2 - 4 * q.y * f3, 2 * q.x * f1 + 2 * q.y * f2};
    const double gn = std::sqrt(g[0] * g[0] + g[1] * g[1] + g[2] * g[2] + g[3] * g[3]);
    const Q w = mul(q, {0, gx, gy, gz});
    Q d = {0.5 * w.w - beta * g[0] / gn, 0.5 * w.x - beta * g[1] / gn, 0.5 * w.y - beta * g[2] / gn,
           0.5 * w.z - beta * g[3] / gn};
    Q r = {q.w + d.w * dt, q.x + d.x * dt, q.y + d.y * dt, q.z + d.z * dt};
    const double rn = std::sqrt(r.w * r.w + r.x * r.x + r.y * r.y + r.z * r.z);
    return {r.w / rn, r.x / rn, r.y / rn, r.z / rn};
}
} // namespace

TEST_CASE(imu_single_step_matches_the_madgwick_equations) {
    // 全成分が非 0 の姿勢・角速度・加速度で、実装と論文の式が一致する
    const struct {
        Q q;
        double g[3], a[3];
    } cases[] = {
        {{0.5, 0.5, 0.5, 0.5}, {0.3, -0.4, 0.5}, {0.2, -0.5, 0.8}},
        {{0.8, 0.2, -0.3, 0.4472135955}, {-1.0, 2.0, 0.5}, {-0.3, 0.1, 0.9}},
        {{0.1, -0.7, 0.3, 0.6403124237}, {2.0, 0.0, -1.5}, {0.6, 0.6, -0.2}},
        {{0.6, 0.3, 0.2, -0.7}, {0.0, 0.0, 0.0}, {1.0, 2.0, 3.0}},
    };
    for (const auto &c : cases) {
        const double qn = std::sqrt(c.q.w * c.q.w + c.q.x * c.q.x + c.q.y * c.q.y + c.q.z * c.q.z);
        const Q q0{c.q.w / qn, c.q.x / qn, c.q.y / qn, c.q.z / qn};
        for (double beta : {0.1, 0.6}) {
            imu_filter_t f;
            imu_filter_init(&f, 50.0f, float(beta));
            f.q = {float(q0.w), float(q0.x), float(q0.y), float(q0.z)};
            imu_filter_update(&f, float(c.g[0]), float(c.g[1]), float(c.g[2]), float(c.a[0]), float(c.a[1]),
                              float(c.a[2]));
            const Q want = reference_step({double(f.q.q0 * 0 + float(q0.w)), double(float(q0.x)), double(float(q0.y)),
                                           double(float(q0.z))},
                                          double(float(c.g[0])), double(float(c.g[1])), double(float(c.g[2])),
                                          double(float(c.a[0])), double(float(c.a[1])), double(float(c.a[2])), beta,
                                          1.0 / 50.0);
            CHECK_NEAR(f.q.q0, want.w, 2e-6);
            CHECK_NEAR(f.q.q1, want.x, 2e-6);
            CHECK_NEAR(f.q.q2, want.y, 2e-6);
            CHECK_NEAR(f.q.q3, want.z, 2e-6);
        }
    }
}

TEST_CASE(imu_gyro_only_integration_about_a_general_axis) {
    // 角速度 (1, 2, 3) rad/s は固定軸まわりの回転。1 s 積分 → q = (cos(|w|/2), sin(|w|/2) w/|w|)
    const double w[3] = {1.0, 2.0, 3.0};
    const double mag = std::sqrt(14.0);
    imu_filter_t f;
    imu_filter_init(&f, 4000.0f, 0.1f);
    for (int i = 0; i < 4000; ++i)
        imu_filter_update(&f, float(w[0]), float(w[1]), float(w[2]), 0, 0, 0);
    CHECK_NEAR(f.q.q0, std::cos(mag / 2), 2e-3);
    CHECK_NEAR(f.q.q1, std::sin(mag / 2) * w[0] / mag, 2e-3);
    CHECK_NEAR(f.q.q2, std::sin(mag / 2) * w[1] / mag, 2e-3);
    CHECK_NEAR(f.q.q3, std::sin(mag / 2) * w[2] / mag, 2e-3);
}

TEST_CASE(imu_keeps_yaw_while_gravity_corrects_roll_and_pitch) {
    // ヨー 60° の姿勢から、ロール 30° / ピッチ -20° の重力を与える。ヨーは重力で観測できず、
    // 勾配降下は最短経路で傾きを直すので多少動くが(約 0.1 rad)、大きくは崩れない
    imu_filter_t f;
    imu_filter_init(&f, 100.0f, 0.1f);
    f.q = axis_angle(0, 0, 1, kPi / 3);
    const double roll = 30 * kPi / 180, pitch = -20 * kPi / 180;
    const float ax = float(-std::sin(pitch)), ay = float(std::sin(roll) * std::cos(pitch)),
                az = float(std::cos(roll) * std::cos(pitch));
    for (int i = 0; i < 3000; ++i)
        imu_filter_update(&f, 0, 0, 0, ax, ay, az);
    euler_angles_t e;
    imu_filter_get_euler(&f, &e);
    CHECK_NEAR(e.roll, roll, 0.02);
    CHECK_NEAR(e.pitch, pitch, 0.02);
    CHECK_NEAR(e.yaw, kPi / 3, 0.15);
}

TEST_CASE(imu_ignores_accel_and_init_values_at_the_epsilon_boundary) {
    // 加速度の大きさがちょうど EPSILON (1e-5) のときは「無し」とみなし、重力補正をしない
    imu_filter_t f;
    imu_filter_init(&f, 100.0f, 0.5f);
    imu_filter_update(&f, 0, 0, 0, 0.00001f, 0, 0);
    CHECK_NEAR(f.q.q0, 1.0, 1e-7);
    CHECK_NEAR(f.q.q2, 0.0, 1e-7);
    imu_filter_update(&f, 0, 0, 0, 0.0001f, 0, 0); // それより大きければ補正する
    CHECK(std::fabs(f.q.q2) > 1e-4);

    // カルマンの R も同様: R == EPSILON は不正値として 0.01 に置き換える
    kalman1d_t kf;
    kalman1d_init(&kf, 0.0f, 0.0f, 0.0f, 0.00001f);
    CHECK_NEAR(kf.r, 0.01, 1e-7);
    kalman1d_init(&kf, 0.0f, 0.0f, 0.0f, 0.00002f);
    CHECK_NEAR(kf.r, 0.00002, 1e-9);
}

EMBED_TEST_MAIN()
