#include <iostream>
#include "embed_test/embed_test.hpp"
#include <cmath>

#include "embed_kinematics/kinematics_types.h"
#include "embed_kinematics/kinematics.h"
#include "embed_kinematics/embed_kinematics.hpp"

static bool approx_eq(float a, float b, float eps = 0.001f) {
    return fabsf(a - b) <= eps;
}

TEST_CASE(test_diff_drive) {
    std::cout << "# [Test] Running Differential Drive tests..." << std::endl;
    // R = 0.05m (5cm), L = 0.30m (30cm)
    embed_kinematics::DiffDrive diff(0.05f, 0.30f);

    // Forward kinematics: left=10 rad/s (0.5m/s), right=10 rad/s (0.5m/s) -> vx=0.5, w=0
    twist2d_t tw = diff.forward(10.0f, 10.0f);
    CHECK(approx_eq(tw.vx, 0.5f));
    CHECK(approx_eq(tw.omega, 0.0f));

    // Inverse kinematics: target vx=0.5m/s, w=1.0 rad/s
    // v_r = 0.5 + 0.5*1.0*0.3 = 0.65m/s -> 0.65 / 0.05 = 13.0 rad/s
    // v_l = 0.5 - 0.5*1.0*0.3 = 0.35m/s -> 0.35 / 0.05 = 7.0 rad/s
    diff_wheel_speeds_t ws = diff.inverse(0.5f, 1.0f);
    CHECK(approx_eq(ws.right, 13.0f));
    CHECK(approx_eq(ws.left, 7.0f));

    // Odometry update: straight 1.0m
    pose2d_t pose{0.0f, 0.0f, 0.0f};
    diff.update_odometry(pose, 1.0f, 1.0f);
    CHECK(approx_eq(pose.x, 1.0f));
    CHECK(approx_eq(pose.y, 0.0f));
    CHECK(approx_eq(pose.theta, 0.0f));

    std::cout << "# [Test] Differential Drive passed!" << std::endl;
}

TEST_CASE(test_mecanum_drive) {
    std::cout << "# [Test] Running Mecanum Drive tests..." << std::endl;
    // R = 0.05m, Lx = 0.4m, Ly = 0.3m -> k = 0.5*(0.4+0.3) = 0.35m
    embed_kinematics::MecanumDrive mecanum(0.05f, 0.4f, 0.3f);

    // Pure Strafe sideways: vy = 0.5 m/s, vx = 0, w = 0
    mecanum_wheel_speeds_t ws = mecanum.inverse(0.0f, 0.5f, 0.0f);
    // fl = -10, fr = 10, rl = 10, rr = -10 (0.5 / 0.05 = 10)
    CHECK(approx_eq(ws.fl, -10.0f));
    CHECK(approx_eq(ws.fr, 10.0f));
    CHECK(approx_eq(ws.rl, 10.0f));
    CHECK(approx_eq(ws.rr, -10.0f));

    twist2d_t tw = mecanum.forward(ws);
    CHECK(approx_eq(tw.vx, 0.0f));
    CHECK(approx_eq(tw.vy, 0.5f));
    CHECK(approx_eq(tw.omega, 0.0f));

    std::cout << "# [Test] Mecanum Drive passed!" << std::endl;
}

TEST_CASE(test_arm2dof) {
    std::cout << "# [Test] Running Arm 2-DOF tests..." << std::endl;
    // L1 = 1.0m, L2 = 1.0m
    embed_kinematics::Arm2DOF arm(1.0f, 1.0f);

    float x = 0.0f, y = 0.0f;
    // q1 = 0, q2 = 0 -> x = 2.0, y = 0.0
    CHECK(arm.forward(0.0f, 0.0f, x, y));
    CHECK(approx_eq(x, 2.0f));
    CHECK(approx_eq(y, 0.0f));

    // Inverse kinematics for (x=0.0, y=1.414) -> sqrt(2)
    float q1 = 0.0f, q2 = 0.0f;
    CHECK(arm.inverse(0.0f, 1.41421356f, true, q1, q2));
    // Verify forward matches
    float fx = 0.0f, fy = 0.0f;
    CHECK(arm.forward(q1, q2, fx, fy));
    CHECK(approx_eq(fx, 0.0f, 0.01f));
    CHECK(approx_eq(fy, 1.41421356f, 0.01f));

    // Unreachable point (x=3.0, y=0.0 > L1+L2)
    CHECK(!arm.inverse(3.0f, 0.0f, true, q1, q2));

    std::cout << "# [Test] Arm 2-DOF passed!" << std::endl;
}

EMBED_TEST_MAIN()
