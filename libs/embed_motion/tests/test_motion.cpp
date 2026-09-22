#include <iostream>
#include <vector>
#include "embed_test/embed_test.hpp"
#include <cmath>

#include "embed_motion/motion_types.h"
#include "embed_motion/motion_profile.h"
#include "embed_motion/embed_motion.hpp"

#define EPSILON 0.01f

static bool approx_eq(float a, float b, float eps = 0.05f) {
    return fabsf(a - b) <= eps;
}

TEST_CASE(test_trapezoidal_profile) {
    std::cout << "# [Test] Running Trapezoidal Profile tests..." << std::endl;

    motion_constraints_t cons{100.0f, 200.0f, 200.0f, 0.0f}; // Vmax=100, A=200, D=200
    motion_profile_t prof;
    bool ok = motion_profile_plan(&prof, MOTION_PROFILE_TRAPEZOIDAL, &cons, 0.0f, 100.0f, 0.0f);
    CHECK(ok);
    CHECK(prof.total_time > 0.0f);

    // 加速時間 = 100/200 = 0.5s, 減速時間 = 0.5s
    // 加速距離 = 0.5 * 200 * 0.25 = 25, 減速距離 = 25
    // 等速距離 = 50 -> 等速時間 = 50 / 100 = 0.5s
    // 総時間 = 1.5s
    CHECK(approx_eq(prof.t_acc, 0.5f));
    CHECK(approx_eq(prof.t_const, 0.5f));
    CHECK(approx_eq(prof.t_dec, 0.5f));
    CHECK(approx_eq(prof.total_time, 1.5f));

    // 中間地点 (t=0.75s) で最高速度 100.0 に到達
    motion_state_t st;
    motion_profile_evaluate(&prof, 0.75f, &st);
    CHECK(approx_eq(st.velocity, 100.0f));

    // 終了地点 (t=1.5s) で目標位置 100.0, 速度 0.0
    motion_profile_evaluate(&prof, 1.5f, &st);
    CHECK(approx_eq(st.position, 100.0f));
    CHECK(approx_eq(st.velocity, 0.0f));
    CHECK(st.is_finished);

    std::cout << "# [Test] Trapezoidal Profile passed!" << std::endl;
}

TEST_CASE(test_scurve_profile) {
    std::cout << "# [Test] Running S-Curve Profile tests..." << std::endl;

    motion_constraints_t cons{50.0f, 100.0f, 100.0f, 500.0f};
    motion_profile_t prof;
    bool ok = motion_profile_plan(&prof, MOTION_PROFILE_SCURVE, &cons, 10.0f, 60.0f, 0.0f);
    CHECK(ok);

    motion_state_t st;
    // 終了位置評価
    motion_profile_evaluate(&prof, prof.total_time, &st);
    CHECK(approx_eq(st.position, 60.0f));
    CHECK(approx_eq(st.velocity, 0.0f));
    CHECK(st.is_finished);

    std::cout << "# [Test] S-Curve Profile passed!" << std::endl;
}

TEST_CASE(test_multi_axis_interpolation) {
    std::cout << "# [Test] Running Multi-Axis Linear Interpolation tests..." << std::endl;

    embed_motion::MultiAxis<3> multi(100.0f, 200.0f, 200.0f);
    std::array<float, 3> start{0.0f, 0.0f, 0.0f};
    std::array<float, 3> target{30.0f, 40.0f, 0.0f}; // ベクトル距離 50.0

    bool ok = multi.plan(start, target);
    CHECK(ok);

    std::array<float, 3> pos{}, vel{};
    float dt = 0.01f;
    bool finished = false;
    while (!finished) {
        finished = multi.step(dt, pos, vel);
    }

    CHECK(approx_eq(pos[0], 30.0f));
    CHECK(approx_eq(pos[1], 40.0f));
    CHECK(approx_eq(pos[2], 0.0f));

    std::cout << "# [Test] Multi-Axis Linear Interpolation passed!" << std::endl;
}

EMBED_TEST_MAIN()
