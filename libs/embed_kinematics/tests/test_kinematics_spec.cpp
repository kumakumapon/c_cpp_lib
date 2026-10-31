// 差動2輪 (順 / 逆 / オドメトリの円弧)、メカナム (全方向の手計算値と往復)、2自由度アーム
// (肘上 / 肘下の解・到達限界・特異点) を、幾何学的に決まる値で固定するテスト。
#include "embed_kinematics/kinematics.h"
#include "embed_test/embed_test.hpp"
#include <cmath>
#include <initializer_list>

namespace {
// 一時オブジェクトを const 参照で受けて、同じ式の間だけ有効なポインタとして渡す
template <class T> const T *tmp(const T &v) { return &v; }
constexpr float kPi = 3.14159265358979f;
constexpr float kTol = 1e-4f;

float wrap(float a) {
    while (a > kPi) a -= 2 * kPi;
    while (a < -kPi) a += 2 * kPi;
    return a;
}
} // namespace

// ---- 差動2輪 ----
TEST_CASE(diff_forward_known_values) {
    diff_drive_params_t p{0.05f, 0.3f};
    twist2d_t t{9, 9, 9};
    diff_drive_forward(&p, tmp(diff_wheel_speeds_t{10.0f, 10.0f}), &t); // 直進: v = ω_wheel * R
    CHECK_NEAR(t.vx, 0.5f, kTol);
    CHECK_NEAR(t.vy, 0.0f, 0.0f);
    CHECK_NEAR(t.omega, 0.0f, kTol);

    diff_drive_forward(&p, tmp(diff_wheel_speeds_t{10.0f, 20.0f}), &t); // v_l = 0.5, v_r = 1.0
    CHECK_NEAR(t.vx, 0.75f, kTol);
    CHECK_NEAR(t.omega, 0.5f / 0.3f, kTol);

    diff_drive_forward(&p, tmp(diff_wheel_speeds_t{-10.0f, 10.0f}), &t); // その場回転
    CHECK_NEAR(t.vx, 0.0f, kTol);
    CHECK_NEAR(t.omega, 1.0f / 0.3f, kTol);

    diff_drive_forward(&p, tmp(diff_wheel_speeds_t{20.0f, 10.0f}), &t); // 右旋回は ω < 0
    CHECK(t.omega < 0.0f);
}

TEST_CASE(diff_forward_degenerate_and_null) {
    twist2d_t t{1, 2, 3};
    diff_drive_params_t tiny{0.05f, 0.00001f}; // トレッド幅が小さすぎる: ω = 0
    diff_drive_forward(&tiny, tmp(diff_wheel_speeds_t{1.0f, 5.0f}), &t);
    CHECK_NEAR(t.omega, 0.0f, 0.0f);
    CHECK_NEAR(t.vx, 0.15f, kTol);
    diff_drive_params_t above{0.05f, 0.00002f}; // 閾値を超えれば計算する
    diff_drive_forward(&above, tmp(diff_wheel_speeds_t{1.0f, 5.0f}), &t);
    CHECK_NEAR(t.omega, 0.2f / 0.00002f, 1.0f);

    t = {1, 2, 3};
    diff_drive_forward(nullptr, tmp(diff_wheel_speeds_t{1, 1}), &t);
    diff_drive_forward(&above, nullptr, &t);
    diff_drive_forward(&above, tmp(diff_wheel_speeds_t{1, 1}), nullptr);
    CHECK_NEAR(t.vx, 1.0f, 0.0f); // 何も書かない
    CHECK_NEAR(t.omega, 3.0f, 0.0f);
}

TEST_CASE(diff_inverse_known_values_and_roundtrip) {
    diff_drive_params_t p{0.05f, 0.3f};
    diff_wheel_speeds_t w{};
    diff_drive_inverse(&p, tmp(twist2d_t{0.5f, 0.0f, 0.0f}), &w);
    CHECK_NEAR(w.left, 10.0f, kTol);
    CHECK_NEAR(w.right, 10.0f, kTol);
    diff_drive_inverse(&p, tmp(twist2d_t{0.0f, 0.0f, 1.0f}), &w); // 左右の周速差 = ω L
    CHECK_NEAR(w.right, 3.0f, kTol);
    CHECK_NEAR(w.left, -3.0f, kTol);
    diff_drive_inverse(&p, tmp(twist2d_t{0.2f, 0.0f, -0.4f}), &w);
    CHECK_NEAR(w.right, (0.2f - 0.5f * 0.4f * 0.3f) / 0.05f, kTol);
    CHECK_NEAR(w.left, (0.2f + 0.5f * 0.4f * 0.3f) / 0.05f, kTol);

    for (float v : {-1.0f, 0.0f, 0.7f})
        for (float om : {-2.0f, 0.0f, 1.3f}) {
            twist2d_t in{v, 0.0f, om}, out{};
            diff_drive_inverse(&p, &in, &w);
            diff_drive_forward(&p, &w, &out);
            CHECK_NEAR(out.vx, v, kTol);
            CHECK_NEAR(out.omega, om, kTol);
        }

    // 車輪半径が小さすぎるときは 1.0 として扱う
    diff_drive_params_t zr{0.0f, 0.3f};
    diff_drive_inverse(&zr, tmp(twist2d_t{0.5f, 0.0f, 0.0f}), &w);
    CHECK_NEAR(w.left, 0.5f, kTol);
    diff_drive_params_t eps{0.00001f, 0.3f};
    diff_drive_inverse(&eps, tmp(twist2d_t{0.5f, 0.0f, 0.0f}), &w);
    CHECK_NEAR(w.left, 0.5f, kTol);
    diff_drive_params_t small{0.00002f, 0.3f};
    diff_drive_inverse(&small, tmp(twist2d_t{0.5f, 0.0f, 0.0f}), &w);
    CHECK_NEAR(w.left, 0.5f / 0.00002f, 10.0f);

    diff_wheel_speeds_t keep{7, 8};
    diff_drive_inverse(nullptr, tmp(twist2d_t{1, 0, 0}), &keep);
    diff_drive_inverse(&p, nullptr, &keep);
    diff_drive_inverse(&p, tmp(twist2d_t{1, 0, 0}), nullptr);
    CHECK_NEAR(keep.left, 7.0f, 0.0f);
}

TEST_CASE(odometry_straight_and_in_place_rotation) {
    pose2d_t pose{0, 0, 0};
    diff_drive_odometry_update(&pose, 1.0f, 1.0f, 0.3f); // 直進
    CHECK_NEAR(pose.x, 1.0f, kTol);
    CHECK_NEAR(pose.y, 0.0f, kTol);
    CHECK_NEAR(pose.theta, 0.0f, kTol);

    pose = {0, 0, kPi / 2};
    diff_drive_odometry_update(&pose, 0.5f, 0.5f, 0.3f); // +y 向きの直進
    CHECK_NEAR(pose.x, 0.0f, kTol);
    CHECK_NEAR(pose.y, 0.5f, kTol);

    pose = {1, 2, 0};
    diff_drive_odometry_update(&pose, -0.15f, 0.15f, 0.3f); // その場回転: dθ = 0.3/0.3 = 1 rad
    CHECK_NEAR(pose.x, 1.0f, kTol);
    CHECK_NEAR(pose.y, 2.0f, kTol);
    CHECK_NEAR(pose.theta, 1.0f, kTol);

    diff_drive_odometry_update(&pose, 0.15f, -0.15f, 0.3f); // 逆向きに戻る
    CHECK_NEAR(pose.theta, 0.0f, kTol);
}

TEST_CASE(odometry_arc_has_radius_v_over_omega) {
    // 中心線の半径 R = 1, トレッド 0.2, 旋回角 φ = π/2 を 1 ステップで進める
    const float R = 1.0f, L = 0.2f, phi = kPi / 2;
    const float dl = (R - L / 2) * phi, dr = (R + L / 2) * phi;
    pose2d_t pose{0, 0, 0};
    diff_drive_odometry_update(&pose, dl, dr, L);
    // 円弧の終点: (R sin φ, R (1 - cos φ)) = (1, 1)
    CHECK_NEAR(pose.x, 1.0f, 1e-3f);
    CHECK_NEAR(pose.y, 1.0f, 1e-3f);
    CHECK_NEAR(pose.theta, kPi / 2, kTol);

    // 初期方位 +90° なら、終点は (−R (1 − cos φ) ... ) = (-1, 1)
    pose = {0, 0, kPi / 2};
    diff_drive_odometry_update(&pose, dl, dr, L);
    CHECK_NEAR(pose.x, -1.0f, 1e-3f);
    CHECK_NEAR(pose.y, 1.0f, 1e-3f);
    CHECK_NEAR(std::fabs(pose.theta), kPi, kTol); // ±π のどちらでもよい

    // 右回りは y が負
    pose = {0, 0, 0};
    diff_drive_odometry_update(&pose, dr, dl, L);
    CHECK_NEAR(pose.x, 1.0f, 1e-3f);
    CHECK_NEAR(pose.y, -1.0f, 1e-3f);
    CHECK_NEAR(pose.theta, -kPi / 2, kTol);

    // 半周 (φ = π) を 1 ステップ: 終点は (0, 2R)
    pose = {0, 0, 0};
    diff_drive_odometry_update(&pose, (R - L / 2) * kPi, (R + L / 2) * kPi, L);
    CHECK_NEAR(pose.x, 0.0f, 1e-3f);
    CHECK_NEAR(pose.y, 2.0f, 1e-3f);
}

TEST_CASE(odometry_small_steps_close_the_circle) {
    // 細かいステップで 1 周すると元の位置・向きに戻る
    const float R = 0.5f, L = 0.2f;
    const int n = 2000;
    const float phi = 2 * kPi / n;
    pose2d_t pose{0, 0, 0};
    float max_x = 0.0f, max_y = 0.0f;
    for (int i = 0; i < n; ++i) {
        diff_drive_odometry_update(&pose, (R - L / 2) * phi, (R + L / 2) * phi, L);
        if (pose.x > max_x) max_x = pose.x;
        if (pose.y > max_y) max_y = pose.y;
    }
    CHECK_NEAR(pose.x, 0.0f, 2e-3f);
    CHECK_NEAR(pose.y, 0.0f, 2e-3f);
    CHECK_NEAR(wrap(pose.theta), 0.0f, 2e-3f);
    CHECK_NEAR(max_y, 2 * R, 3e-3f); // 直径
    CHECK_NEAR(max_x, R, 3e-3f);
}

TEST_CASE(odometry_angle_is_normalized) {
    pose2d_t pose{0, 0, 3.0f};
    diff_drive_odometry_update(&pose, 0.0f, 0.15f, 0.3f); // +0.5 rad -> 3.5 -> 3.5 - 2π
    CHECK_NEAR(pose.theta, 3.5f - 2 * kPi, kTol);
    pose = {0, 0, -3.0f};
    diff_drive_odometry_update(&pose, 0.15f, 0.0f, 0.3f); // -0.5 rad -> -3.5 -> -3.5 + 2π
    CHECK_NEAR(pose.theta, -3.5f + 2 * kPi, kTol);
    pose = {0, 0, 0.0f};
    diff_drive_odometry_update(&pose, -1.5f, 1.5f, 0.3f); // dθ = 10 rad: 何周しても (-π, π] に収まる
    CHECK(pose.theta <= kPi);
    CHECK(pose.theta > -kPi);
    CHECK_NEAR(pose.theta, wrap(10.0f), 1e-3f);
}

TEST_CASE(odometry_invalid_inputs) {
    pose2d_t pose{1, 2, 0.5f};
    diff_drive_odometry_update(&pose, 1.0f, 1.0f, 0.0f); // トレッド幅 0 は何もしない
    diff_drive_odometry_update(&pose, 1.0f, 1.0f, 0.000001f);
    diff_drive_odometry_update(nullptr, 1.0f, 1.0f, 0.3f);
    CHECK_NEAR(pose.x, 1.0f, 0.0f);
    CHECK_NEAR(pose.y, 2.0f, 0.0f);
    CHECK_NEAR(pose.theta, 0.5f, 0.0f);
    // 閾値 (1e-5) 未満は何もしない。ちょうど閾値以上なら進める
    diff_drive_odometry_update(&pose, 1.0f, 1.0f, 0.000009f);
    CHECK_NEAR(pose.x, 1.0f, 0.0f);
    diff_drive_odometry_update(&pose, 1.0f, 1.0f, 0.00001f);
    CHECK(pose.x > 1.0f);
}

// ---- メカナム ----
TEST_CASE(mecanum_forward_known_values) {
    mecanum_params_t p{0.05f, 0.4f, 0.3f}; // k = (0.4 + 0.3) / 2 = 0.35
    twist2d_t t{};
    mecanum_drive_forward(&p, tmp(mecanum_wheel_speeds_t{10, 10, 10, 10}), &t); // 前進
    CHECK_NEAR(t.vx, 0.5f, kTol);
    CHECK_NEAR(t.vy, 0.0f, kTol);
    CHECK_NEAR(t.omega, 0.0f, kTol);
    mecanum_drive_forward(&p, tmp(mecanum_wheel_speeds_t{-10, 10, 10, -10}), &t); // 左横移動
    CHECK_NEAR(t.vx, 0.0f, kTol);
    CHECK_NEAR(t.vy, 0.5f, kTol);
    CHECK_NEAR(t.omega, 0.0f, kTol);
    mecanum_drive_forward(&p, tmp(mecanum_wheel_speeds_t{-10, 10, -10, 10}), &t); // 左旋回
    CHECK_NEAR(t.vx, 0.0f, kTol);
    CHECK_NEAR(t.vy, 0.0f, kTol);
    CHECK_NEAR(t.omega, 0.25f * 0.05f / 0.35f * 40.0f, kTol);
    mecanum_drive_forward(&p, tmp(mecanum_wheel_speeds_t{0, 20, 20, 0}), &t); // 斜め前左
    CHECK_NEAR(t.vx, 0.5f, kTol);
    CHECK_NEAR(t.vy, 0.5f, kTol);
    CHECK_NEAR(t.omega, 0.0f, kTol);
    mecanum_drive_forward(&p, tmp(mecanum_wheel_speeds_t{10, 0, 0, 0}), &t); // 各輪の寄与の符号
    CHECK_NEAR(t.vx, 0.125f, kTol);
    CHECK_NEAR(t.vy, -0.125f, kTol);
    CHECK(t.omega < 0.0f);
    mecanum_drive_forward(&p, tmp(mecanum_wheel_speeds_t{0, 0, 10, 0}), &t);
    CHECK_NEAR(t.vy, 0.125f, kTol);
    CHECK(t.omega < 0.0f);
    mecanum_drive_forward(&p, tmp(mecanum_wheel_speeds_t{0, 0, 0, 10}), &t);
    CHECK_NEAR(t.vy, -0.125f, kTol);
    CHECK(t.omega > 0.0f);
    mecanum_drive_forward(&p, tmp(mecanum_wheel_speeds_t{0, 10, 0, 0}), &t);
    CHECK_NEAR(t.vy, 0.125f, kTol);
    CHECK(t.omega > 0.0f);

    mecanum_params_t flat{0.05f, 0.000004f, 0.000004f}; // k が小さすぎる: ω = 0
    mecanum_drive_forward(&flat, tmp(mecanum_wheel_speeds_t{-10, 10, -10, 10}), &t);
    CHECK_NEAR(t.omega, 0.0f, 0.0f);
}

TEST_CASE(mecanum_inverse_known_values) {
    mecanum_params_t p{0.05f, 0.4f, 0.3f};
    mecanum_wheel_speeds_t w{};
    mecanum_drive_inverse(&p, tmp(twist2d_t{0.5f, 0.0f, 0.0f}), &w);
    CHECK_NEAR(w.fl, 10.0f, kTol);
    CHECK_NEAR(w.fr, 10.0f, kTol);
    CHECK_NEAR(w.rl, 10.0f, kTol);
    CHECK_NEAR(w.rr, 10.0f, kTol);
    mecanum_drive_inverse(&p, tmp(twist2d_t{0.0f, 0.5f, 0.0f}), &w);
    CHECK_NEAR(w.fl, -10.0f, kTol);
    CHECK_NEAR(w.fr, 10.0f, kTol);
    CHECK_NEAR(w.rl, 10.0f, kTol);
    CHECK_NEAR(w.rr, -10.0f, kTol);
    mecanum_drive_inverse(&p, tmp(twist2d_t{0.0f, 0.0f, 1.0f}), &w); // k ω / R = 7
    CHECK_NEAR(w.fl, -7.0f, kTol);
    CHECK_NEAR(w.fr, 7.0f, kTol);
    CHECK_NEAR(w.rl, -7.0f, kTol);
    CHECK_NEAR(w.rr, 7.0f, kTol);
    mecanum_drive_inverse(&p, tmp(twist2d_t{0.5f, 0.5f, 0.0f}), &w); // 斜め
    CHECK_NEAR(w.fl, 0.0f, kTol);
    CHECK_NEAR(w.fr, 20.0f, kTol);
    CHECK_NEAR(w.rl, 20.0f, kTol);
    CHECK_NEAR(w.rr, 0.0f, kTol);

    mecanum_params_t zr{0.0f, 0.4f, 0.3f}; // 半径が小さすぎるときは 1.0 として扱う
    mecanum_drive_inverse(&zr, tmp(twist2d_t{0.5f, 0.0f, 0.0f}), &w);
    CHECK_NEAR(w.fl, 0.5f, kTol);
    mecanum_params_t eps{0.00001f, 0.4f, 0.3f};
    mecanum_drive_inverse(&eps, tmp(twist2d_t{0.5f, 0.0f, 0.0f}), &w);
    CHECK_NEAR(w.fl, 0.5f, kTol);

    mecanum_wheel_speeds_t keep{1, 2, 3, 4};
    mecanum_drive_inverse(nullptr, tmp(twist2d_t{1, 0, 0}), &keep);
    mecanum_drive_inverse(&p, nullptr, &keep);
    mecanum_drive_inverse(&p, tmp(twist2d_t{1, 0, 0}), nullptr);
    CHECK_NEAR(keep.fl, 1.0f, 0.0f);
    twist2d_t keep_t{1, 2, 3};
    mecanum_drive_forward(nullptr, tmp(mecanum_wheel_speeds_t{1, 1, 1, 1}), &keep_t);
    mecanum_drive_forward(&p, nullptr, &keep_t);
    mecanum_drive_forward(&p, tmp(mecanum_wheel_speeds_t{1, 1, 1, 1}), nullptr);
    CHECK_NEAR(keep_t.vx, 1.0f, 0.0f);
}

TEST_CASE(mecanum_roundtrip_over_twists) {
    mecanum_params_t p{0.04f, 0.25f, 0.2f};
    for (float vx : {-1.0f, 0.0f, 0.6f})
        for (float vy : {-0.8f, 0.0f, 0.4f})
            for (float om : {-2.0f, 0.0f, 1.5f}) {
                twist2d_t in{vx, vy, om}, out{};
                mecanum_wheel_speeds_t w{};
                mecanum_drive_inverse(&p, &in, &w);
                mecanum_drive_forward(&p, &w, &out);
                CHECK_NEAR(out.vx, vx, kTol);
                CHECK_NEAR(out.vy, vy, kTol);
                CHECK_NEAR(out.omega, om, kTol);
            }
}

// ---- 2自由度アーム ----
TEST_CASE(arm_forward_known_values) {
    arm2dof_params_t p{1.0f, 1.0f};
    float x = 9, y = 9;
    CHECK(arm2dof_forward(&p, tmp(arm2dof_joints_t{0, 0}), &x, &y)); // 完全伸展
    CHECK_NEAR(x, 2.0f, kTol);
    CHECK_NEAR(y, 0.0f, kTol);
    arm2dof_forward(&p, tmp(arm2dof_joints_t{kPi / 2, 0}), &x, &y);
    CHECK_NEAR(x, 0.0f, kTol);
    CHECK_NEAR(y, 2.0f, kTol);
    arm2dof_forward(&p, tmp(arm2dof_joints_t{0, kPi / 2}), &x, &y);
    CHECK_NEAR(x, 1.0f, kTol);
    CHECK_NEAR(y, 1.0f, kTol);
    arm2dof_forward(&p, tmp(arm2dof_joints_t{kPi / 2, kPi / 2}), &x, &y);
    CHECK_NEAR(x, -1.0f, kTol);
    CHECK_NEAR(y, 1.0f, kTol);
    arm2dof_forward(&p, tmp(arm2dof_joints_t{0, kPi}), &x, &y); // 完全折りたたみ
    CHECK_NEAR(x, 0.0f, kTol);
    CHECK_NEAR(y, 0.0f, kTol);

    arm2dof_params_t q{0.5f, 0.2f}; // リンク長が異なる
    arm2dof_forward(&q, tmp(arm2dof_joints_t{kPi / 2, -kPi / 2}), &x, &y);
    CHECK_NEAR(x, 0.2f, kTol);
    CHECK_NEAR(y, 0.5f, kTol);

    CHECK(!arm2dof_forward(nullptr, tmp(arm2dof_joints_t{0, 0}), &x, &y));
    CHECK(!arm2dof_forward(&p, nullptr, &x, &y));
    CHECK(!arm2dof_forward(&p, tmp(arm2dof_joints_t{0, 0}), nullptr, &y));
    CHECK(!arm2dof_forward(&p, tmp(arm2dof_joints_t{0, 0}), &x, nullptr));
}

TEST_CASE(arm_inverse_elbow_up_and_down) {
    arm2dof_params_t p{1.0f, 1.0f};
    arm2dof_joints_t j{};
    CHECK(arm2dof_inverse(&p, 1.0f, 1.0f, true, &j)); // 肘上: q2 > 0
    CHECK_NEAR(j.q1, 0.0f, kTol);
    CHECK_NEAR(j.q2, kPi / 2, kTol);
    CHECK(arm2dof_inverse(&p, 1.0f, 1.0f, false, &j)); // 肘下: q2 < 0
    CHECK_NEAR(j.q1, kPi / 2, kTol);
    CHECK_NEAR(j.q2, -kPi / 2, kTol);

    CHECK(arm2dof_inverse(&p, 0.0f, 2.0f, true, &j)); // 完全伸展 (特異点): q2 = 0
    CHECK_NEAR(j.q1, kPi / 2, kTol);
    CHECK_NEAR(j.q2, 0.0f, kTol);
    CHECK(arm2dof_inverse(&p, 0.0f, 2.0f, false, &j));
    CHECK_NEAR(j.q1, kPi / 2, kTol);
    CHECK_NEAR(std::fabs(j.q2), 0.0f, kTol);

    CHECK(arm2dof_inverse(&p, 1.0f, 0.0f, true, &j)); // 60° の肘
    CHECK_NEAR(std::fabs(j.q2), 2 * kPi / 3, kTol);
    CHECK_NEAR(j.q1, -kPi / 3, kTol);
}

TEST_CASE(arm_inverse_roundtrip_reaches_both_solutions) {
    arm2dof_params_t p{0.8f, 0.5f};
    for (int i = 0; i < 60; ++i) {
        const float ang = -kPi + (2 * kPi) * i / 60.0f;
        for (float rad : {0.35f, 0.6f, 1.0f, 1.25f}) { // |l1-l2| = 0.3 .. l1+l2 = 1.3
            const float tx = rad * std::cos(ang), ty = rad * std::sin(ang);
            for (bool up : {true, false}) {
                arm2dof_joints_t j{};
                REQUIRE(arm2dof_inverse(&p, tx, ty, up, &j));
                CHECK(up ? j.q2 >= 0.0f : j.q2 <= 0.0f);
                float x = 0, y = 0;
                arm2dof_forward(&p, &j, &x, &y);
                CHECK_NEAR(x, tx, 2e-4f);
                CHECK_NEAR(y, ty, 2e-4f);
            }
        }
    }
}

TEST_CASE(arm_inverse_workspace_limits) {
    arm2dof_params_t p{1.0f, 0.5f};
    arm2dof_joints_t j{9, 9};
    // 外側: l1 + l2 = 1.5 を超えると到達不能 (関節角には触らない)
    CHECK(arm2dof_inverse(&p, 1.5f, 0.0f, true, &j));
    CHECK(!arm2dof_inverse(&p, 1.51f, 0.0f, true, &j));
    CHECK(!arm2dof_inverse(&p, 0.0f, -1.6f, false, &j));
    // 内側: |l1 - l2| = 0.5 より近いと到達不能
    CHECK(arm2dof_inverse(&p, 0.5f, 0.0f, true, &j));
    CHECK_NEAR(std::fabs(j.q2), kPi, kTol);
    CHECK(!arm2dof_inverse(&p, 0.49f, 0.0f, true, &j));
    CHECK(!arm2dof_inverse(&p, 0.0f, 0.0f, true, &j));
    arm2dof_joints_t keep{9, 9};
    CHECK(!arm2dof_inverse(&p, 5.0f, 0.0f, true, &keep));
    CHECK_NEAR(keep.q1, 9.0f, 0.0f);
    CHECK_NEAR(keep.q2, 9.0f, 0.0f);

    CHECK(!arm2dof_inverse(nullptr, 1.0f, 0.0f, true, &j));
    CHECK(!arm2dof_inverse(&p, 1.0f, 0.0f, true, nullptr));
}

TEST_CASE(arm_inverse_reaches_the_exact_limits_for_any_link_lengths) {
    // 丸め誤差で、ちょうど限界の点が「到達不能」にならないこと
    const float lens[][2] = {{0.3f, 0.2f}, {0.1f, 0.7f}, {0.37f, 0.59f}, {1.1f, 0.3f}, {0.123f, 0.456f}};
    for (const auto &l : lens) {
        arm2dof_params_t p{l[0], l[1]};
        for (int i = 0; i < 24; ++i) {
            const float ang = -kPi + 2 * kPi * i / 24.0f;
            for (float rad : {l[0] + l[1], std::fabs(l[0] - l[1])}) {
                arm2dof_joints_t j{};
                CHECK(arm2dof_inverse(&p, rad * std::cos(ang), rad * std::sin(ang), true, &j));
            }
        }
    }
}

TEST_CASE(arm_inverse_rejects_degenerate_links_and_nan) {
    arm2dof_joints_t j{9, 9};
    arm2dof_params_t zero1{0.0f, 1.0f}, zero2{1.0f, 0.0f};
    CHECK(!arm2dof_inverse(&zero1, 1.0f, 0.0f, true, &j));
    CHECK(!arm2dof_inverse(&zero2, 1.0f, 0.0f, true, &j));
    arm2dof_params_t p{1.0f, 1.0f};
    const float nan = std::nanf("");
    CHECK(!arm2dof_inverse(&p, nan, 0.0f, true, &j));
    CHECK(!arm2dof_inverse(&p, 1.0f, nan, true, &j));
    CHECK_NEAR(j.q1, 9.0f, 0.0f);
}

EMBED_TEST_MAIN()
