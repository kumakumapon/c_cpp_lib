// モーションプロファイルの解析解(既知値)を固定するテスト。
//
// 等加速度運動の公式 (s = ½at², v = at, s = v²/2a など) から手計算した値を期待値にしている。
// 台形・三角形・非対称加減速・逆方向・開始位置オフセットについて、位相の境界と途中の
// 位置/速度/加速度を直接確認する。S字プロファイルは、実装に依らず満たすべき性質
// (端点条件、対称性、単調性、連続性)を確認する。
#include <cmath>
#include <cstdio>
#include <vector>

#include "embed_motion/motion_profile.h"
#include "embed_motion/motion_types.h"
#include "embed_test/embed_test.hpp"

namespace {

constexpr double kTol = 1e-3;

struct Sample {
    float t;
    float pos;
    float vel;
    float acc;
};

motion_profile_t plan(motion_profile_type_t type, motion_constraints_t c, float from, float to) {
    motion_profile_t p{};
    CHECK(motion_profile_plan(&p, type, &c, from, to, 0.0f));
    return p;
}

motion_state_t at(const motion_profile_t &p, float t) {
    motion_state_t s{};
    motion_profile_evaluate(&p, t, &s);
    return s;
}

void check_samples(const motion_profile_t &p, const std::vector<Sample> &samples) {
    for (const Sample &e : samples) {
        motion_state_t s = at(p, e.t);
        if (!(std::fabs(s.position - e.pos) <= kTol && std::fabs(s.velocity - e.vel) <= kTol &&
              std::fabs(s.acceleration - e.acc) <= kTol)) {
            std::printf("# t=%.4f expected pos=%.4f vel=%.4f acc=%.4f, actual pos=%.4f vel=%.4f acc=%.4f\n", e.t,
                        e.pos, e.vel, e.acc, s.position, s.velocity, s.acceleration);
            CHECK(false);
        } else {
            CHECK(true);
        }
    }
}

} // namespace

// =============================================================================
// 台形: Vmax=100, Amax=Dmax=200, 距離 100
//   加速 0.5 s (25), 等速 0.5 s (50), 減速 0.5 s (25), 総時間 1.5 s
// =============================================================================
TEST_CASE(trapezoid_symmetric_matches_constant_acceleration_formulas) {
    motion_profile_t p = plan(MOTION_PROFILE_TRAPEZOIDAL, {100, 200, 200, 0}, 0, 100);
    CHECK_NEAR(p.t_acc, 0.5, kTol);
    CHECK_NEAR(p.t_const, 0.5, kTol);
    CHECK_NEAR(p.t_dec, 0.5, kTol);
    CHECK_NEAR(p.total_time, 1.5, kTol);
    CHECK_NEAR(p.v_peak, 100.0, kTol);

    check_samples(p, {
                         {0.25f, 6.25f, 50.0f, 200.0f},     // s = ½·200·0.25²
                         {0.50f, 25.0f, 100.0f, 200.0f},    // 加速の終端
                         {0.75f, 50.0f, 100.0f, 0.0f},      // 等速: 25 + 100·0.25
                         {1.00f, 75.0f, 100.0f, 0.0f},      // 等速の終端
                         {1.25f, 93.75f, 50.0f, -200.0f},   // 減速: 75 + 100·0.25 − ½·200·0.25²
                         {1.40f, 99.0f, 20.0f, -200.0f},    // 残り 0.1 s: 残距離 ½·200·0.01 = 1
                     });
    // 総時間以降は目標位置・速度0・完了
    motion_state_t end = at(p, 1.5f);
    CHECK_NEAR(end.position, 100.0, kTol);
    CHECK_NEAR(end.velocity, 0.0, kTol);
    CHECK(end.is_finished);
    end = at(p, 99.0f);
    CHECK_NEAR(end.position, 100.0, kTol);
    CHECK(end.is_finished);
    CHECK(!at(p, 1.49f).is_finished);
}

TEST_CASE(trapezoid_asymmetric_deceleration_is_slower_than_acceleration) {
    // Amax=200, Dmax=100: 加速 0.5 s (25), 減速 1.0 s (50), 等速 25/100 = 0.25 s、総時間 1.75 s
    motion_profile_t p = plan(MOTION_PROFILE_TRAPEZOIDAL, {100, 200, 100, 0}, 0, 100);
    CHECK_NEAR(p.t_acc, 0.5, kTol);
    CHECK_NEAR(p.t_const, 0.25, kTol);
    CHECK_NEAR(p.t_dec, 1.0, kTol);
    CHECK_NEAR(p.total_time, 1.75, kTol);
    check_samples(p, {
                         {0.25f, 6.25f, 50.0f, 200.0f},
                         {0.625f, 37.5f, 100.0f, 0.0f},   // 25 + 100·0.125
                         {1.25f, 87.5f, 50.0f, -100.0f},  // 減速 0.5 s 経過: 50 + 100·0.5 − ½·100·0.25
                         {1.75f, 100.0f, 0.0f, 0.0f},
                     });
}

TEST_CASE(trapezoid_degenerates_to_triangle_when_distance_is_short) {
    // 距離 10 では最高速度に届かない: v = √(2·A·D·s/(A+D)) = √2000 ≈ 44.7214、加速 = 減速 ≈ 0.2236 s
    motion_profile_t p = plan(MOTION_PROFILE_TRAPEZOIDAL, {100, 200, 200, 0}, 0, 10);
    CHECK_NEAR(p.v_peak, std::sqrt(2000.0), kTol);
    CHECK_NEAR(p.t_acc, std::sqrt(2000.0) / 200.0, kTol);
    CHECK_NEAR(p.t_const, 0.0, kTol);
    CHECK_NEAR(p.t_dec, std::sqrt(2000.0) / 200.0, kTol);
    CHECK_NEAR(p.total_time, 2.0 * std::sqrt(2000.0) / 200.0, kTol);
    motion_state_t mid = at(p, p.total_time / 2.0f);
    CHECK_NEAR(mid.position, 5.0, kTol);
    CHECK_NEAR(mid.velocity, std::sqrt(2000.0), 2e-3);

    // 非対称 (A=200, D=50): v = √800 ≈ 28.2843。加速で 2.0、減速で残り 8.0 を使う
    p = plan(MOTION_PROFILE_TRAPEZOIDAL, {100, 200, 50, 0}, 0, 10);
    CHECK_NEAR(p.v_peak, std::sqrt(800.0), kTol);
    CHECK_NEAR(p.t_acc, std::sqrt(800.0) / 200.0, kTol);
    CHECK_NEAR(p.t_dec, std::sqrt(800.0) / 50.0, kTol);
    CHECK_NEAR(at(p, p.t_acc).position, 2.0, kTol);
    CHECK_NEAR(at(p, p.t_acc).velocity, std::sqrt(800.0), 2e-3);
    CHECK_NEAR(at(p, p.total_time).position, 10.0, kTol);
}

TEST_CASE(trapezoid_boundary_between_triangle_and_trapezoid) {
    // 距離 = 加速距離 + 減速距離 = 25 + 25 のちょうど境界: 等速区間は 0 で、最高速度に到達する
    motion_profile_t p = plan(MOTION_PROFILE_TRAPEZOIDAL, {100, 200, 200, 0}, 0, 50);
    CHECK_NEAR(p.v_peak, 100.0, kTol);
    CHECK_NEAR(p.t_const, 0.0, kTol);
    CHECK_NEAR(p.total_time, 1.0, kTol);
    check_samples(p, {{0.5f, 25.0f, 100.0f, 200.0f}, {0.75f, 43.75f, 50.0f, -200.0f}});
    // 境界のわずか手前では三角形(最高速度に届かない)
    p = plan(MOTION_PROFILE_TRAPEZOIDAL, {100, 200, 200, 0}, 0, 49.9f);
    CHECK(p.v_peak < 100.0f);
    CHECK_NEAR(p.t_const, 0.0, kTol);
}

TEST_CASE(trapezoid_negative_direction_and_start_offset_mirror_the_motion) {
    // 50 -> -50 (距離 100): 位置は 50 − s(t)、速度・加速度は符号が反転する
    motion_profile_t p = plan(MOTION_PROFILE_TRAPEZOIDAL, {100, 200, 200, 0}, 50, -50);
    CHECK_NEAR(p.direction, -1.0, kTol);
    CHECK_NEAR(p.distance, 100.0, kTol);
    check_samples(p, {
                         {0.25f, 43.75f, -50.0f, -200.0f},
                         {0.75f, 0.0f, -100.0f, 0.0f},
                         {1.25f, -43.75f, -50.0f, 200.0f},
                         {1.5f, -50.0f, 0.0f, 0.0f},
                     });
    // 開始位置のオフセット: 10 -> 110 は 0 -> 100 の位置を +10 しただけ
    motion_profile_t q = plan(MOTION_PROFILE_TRAPEZOIDAL, {100, 200, 200, 0}, 10, 110);
    check_samples(q, {{0.25f, 16.25f, 50.0f, 200.0f}, {0.75f, 60.0f, 100.0f, 0.0f}, {1.25f, 103.75f, 50.0f, -200.0f}});
}

TEST_CASE(trapezoid_integrates_consistently_and_respects_all_limits) {
    // 細かい刻みで、加速度の積分が速度、速度の積分が位置に一致し、全ての制約を守ること
    const motion_constraints_t c{80.0f, 150.0f, 90.0f, 0.0f};
    motion_profile_t p = plan(MOTION_PROFILE_TRAPEZOIDAL, c, -20, 130);
    const int n = 30000;
    const double dt = p.total_time / n;
    double v_int = 0.0, s_int = 0.0;
    for (int i = 0; i < n; ++i) {
        motion_state_t s = at(p, (float)((i + 0.5) * dt)); // 区間の中点
        CHECK(std::fabs(s.velocity) <= c.max_velocity + 1e-3f);
        CHECK(s.acceleration <= c.max_acceleration + 1e-3f);
        CHECK(s.acceleration >= -c.max_deceleration - 1e-3f);
        v_int += s.acceleration * dt;
        s_int += s.velocity * dt;
    }
    CHECK_NEAR(v_int, 0.0, 0.05); // 加速度の総和は 0 (停止から停止)
    CHECK_NEAR(s_int, 150.0, 0.05); // 速度の積分 = 移動距離
    CHECK_NEAR(at(p, p.total_time).position, 130.0, kTol);
}

TEST_CASE(profile_planning_validates_inputs_and_zero_length_moves) {
    motion_profile_t p{};
    const motion_constraints_t ok{100, 200, 200, 0};
    CHECK(!motion_profile_plan(nullptr, MOTION_PROFILE_TRAPEZOIDAL, &ok, 0, 1, 0));
    CHECK(!motion_profile_plan(&p, MOTION_PROFILE_TRAPEZOIDAL, nullptr, 0, 1, 0));
    for (motion_constraints_t bad : {motion_constraints_t{0, 200, 200, 0}, motion_constraints_t{-1, 200, 200, 0},
                                     motion_constraints_t{100, 0, 200, 0}, motion_constraints_t{100, -5, 200, 0},
                                     motion_constraints_t{100, 200, 0, 0}, motion_constraints_t{100, 200, -5, 0}}) {
        CHECK(!motion_profile_plan(&p, MOTION_PROFILE_TRAPEZOIDAL, &bad, 0, 1, 0));
        CHECK(!motion_profile_plan(&p, MOTION_PROFILE_SCURVE, &bad, 0, 1, 0));
    }

    // 距離 0 は即座に完了。位置は目標位置
    p = plan(MOTION_PROFILE_TRAPEZOIDAL, ok, 5, 5);
    CHECK_NEAR(p.total_time, 0.0, kTol);
    CHECK(p.state.is_finished);
    motion_state_t s = at(p, 0.0f);
    CHECK(s.is_finished);
    CHECK_NEAR(s.position, 5.0, kTol);
    CHECK_NEAR(s.velocity, 0.0, kTol);
    // 1e-5 未満の距離は 0 とみなす。それ以上は実際に動く
    p = plan(MOTION_PROFILE_TRAPEZOIDAL, ok, 0, 5e-6f);
    CHECK_NEAR(p.total_time, 0.0, kTol);
    p = plan(MOTION_PROFILE_TRAPEZOIDAL, ok, 0, 2e-5f);
    CHECK(p.total_time > 0.0f);

    // t <= 0 は開始状態、総時間以上は目標状態
    p = plan(MOTION_PROFILE_TRAPEZOIDAL, ok, 0, 100);
    s = at(p, 0.0f);
    CHECK_NEAR(s.position, 0.0, kTol);
    CHECK(!s.is_finished);
    s = at(p, -3.0f);
    CHECK_NEAR(s.position, 0.0, kTol);
    CHECK(!s.is_finished);
    // evaluate に NULL を渡しても落ちない
    motion_profile_evaluate(nullptr, 0.0f, &s);
    motion_profile_evaluate(&p, 0.0f, nullptr);
}

TEST_CASE(profile_step_advances_time_and_stays_finished) {
    motion_profile_t p = plan(MOTION_PROFILE_TRAPEZOIDAL, {100, 200, 200, 0}, 0, 100);
    // 0.5 s は float で厳密。加速終端 → 等速終端 → 完了
    const motion_state_t *s = motion_profile_step(&p, 0.5f);
    CHECK_NEAR(s->position, 25.0, kTol);
    CHECK_NEAR(s->velocity, 100.0, kTol);
    CHECK(!s->is_finished);
    s = motion_profile_step(&p, 0.5f);
    CHECK_NEAR(s->position, 75.0, kTol);
    CHECK(!s->is_finished);
    s = motion_profile_step(&p, 0.5f);
    CHECK_NEAR(s->position, 100.0, kTol);
    CHECK_NEAR(s->velocity, 0.0, kTol);
    CHECK(s->is_finished);
    // 完了後に進めても位置は目標のまま
    s = motion_profile_step(&p, 1.0f);
    CHECK_NEAR(s->position, 100.0, kTol);
    CHECK(s->is_finished);
    CHECK_NEAR(p.current_time, 2.5, kTol);
    CHECK(motion_profile_step(nullptr, 0.1f) == nullptr);
}

// =============================================================================
// S字: 実装に依らず満たすべき性質
// =============================================================================
TEST_CASE(scurve_has_rest_to_rest_boundary_conditions_and_is_symmetric) {
    const motion_constraints_t c{50, 100, 100, 500};
    motion_profile_t p = plan(MOTION_PROFILE_SCURVE, c, 10, 60);
    REQUIRE(p.total_time > 0.0f);
    const float T = p.total_time;

    // 端点: 位置、速度 0、加速度 0 (滑らかに始まり滑らかに止まる)
    motion_state_t s0 = at(p, 1e-4f);
    CHECK_NEAR(s0.position, 10.0, 1e-2);
    CHECK_NEAR(s0.velocity, 0.0, 0.1);
    CHECK_NEAR(s0.acceleration, 0.0, 0.5);
    motion_state_t s1 = at(p, T - 1e-4f);
    CHECK_NEAR(s1.position, 60.0, 1e-2);
    CHECK_NEAR(s1.velocity, 0.0, 0.1);
    CHECK_NEAR(s1.acceleration, 0.0, 0.5);
    motion_state_t end = at(p, T);
    CHECK_NEAR(end.position, 60.0, kTol);
    CHECK_NEAR(end.velocity, 0.0, kTol);
    CHECK(end.is_finished);

    // 対称性: 中点で移動距離の半分、速度は最大、加速度は 0。両側で位置が点対称
    motion_state_t mid = at(p, T / 2);
    CHECK_NEAR(mid.position, 35.0, 1e-2);
    CHECK_NEAR(mid.acceleration, 0.0, 0.5);
    float vmax = 0;
    for (int i = 1; i < 200; ++i)
        vmax = std::fmax(vmax, at(p, T * i / 200).velocity);
    CHECK_NEAR(mid.velocity, vmax, 0.05);
    for (float frac : {0.1f, 0.25f, 0.4f}) {
        CHECK_NEAR(at(p, T * frac).position + at(p, T * (1 - frac)).position, 70.0, 1e-2); // 10 + 60
        CHECK_NEAR(at(p, T * frac).velocity, at(p, T * (1 - frac)).velocity, 1e-2);
    }
}

TEST_CASE(scurve_position_is_monotonic_and_velocity_is_continuous) {
    for (float to : {60.0f, 5.0f, 300.0f, -40.0f}) {
        const float from = 10.0f;
        motion_profile_t p = plan(MOTION_PROFILE_SCURVE, {50, 100, 100, 500}, from, to);
        REQUIRE(p.total_time > 0.0f);
        const float dir = to > from ? 1.0f : -1.0f;
        const int n = 4000;
        float prev_pos = from, prev_vel = 0.0f;
        float worst_back = 0.0f, worst_dv = 0.0f;
        for (int i = 1; i <= n; ++i) {
            motion_state_t s = at(p, p.total_time * i / n);
            worst_back = std::fmax(worst_back, -(s.position - prev_pos) * dir); // 戻り量 (float 丸めを除き 0)
            worst_dv = std::fmax(worst_dv, std::fabs(s.velocity - prev_vel));
            prev_pos = s.position;
            prev_vel = s.velocity;
        }
        CHECK(worst_back < 1e-3f);
        // 速度は連続: 1 ステップの変化は (最大加速度 x 刻み) 程度に収まる
        const float step_dt = p.total_time / n;
        CHECK(worst_dv < 200.0f * step_dt + 1e-2f);
        // 速度の符号は移動方向に一致する
        for (int i = 1; i < 40; ++i)
            CHECK(at(p, p.total_time * i / 40).velocity * dir >= -1e-3f);
    }
}

TEST_CASE(scurve_is_smoother_than_trapezoid_at_the_start) {
    // 台形は t=0 の直後に最大加速度へ飛ぶ。S字は加速度が連続的に立ち上がる
    motion_profile_t t = plan(MOTION_PROFILE_TRAPEZOIDAL, {50, 100, 100, 500}, 0, 100);
    motion_profile_t s = plan(MOTION_PROFILE_SCURVE, {50, 100, 100, 500}, 0, 100);
    CHECK_NEAR(at(t, 1e-3f).acceleration, 100.0, kTol);
    CHECK(std::fabs(at(s, 1e-3f).acceleration) < 5.0f);
    // 加速度が最大加速度の半分に達するまでに掛かる時間は S字の方が長い
    auto time_to_half = [](const motion_profile_t &p) {
        for (int i = 1; i < 4000; ++i) {
            float tm = p.total_time * i / 4000;
            if (std::fabs(at(p, tm).acceleration) >= 50.0f)
                return tm;
        }
        return p.total_time;
    };
    CHECK(time_to_half(s) > time_to_half(t));
}

// =============================================================================
// 多軸直線補間 (ベクトル距離に対する台形プロファイル)
// =============================================================================
TEST_CASE(multi_axis_follows_the_straight_line_with_vector_limits) {
    // (0,0,0) -> (30,40,0): ベクトル距離 50、方向比 0.6 / 0.8 / 0
    const motion_constraints_t c{100, 200, 200, 0};
    motion_multi_axis_t m{};
    const float start[3] = {0, 0, 0}, target[3] = {30, 40, 0};
    REQUIRE(motion_multi_axis_plan(&m, 3, &c, start, target));
    CHECK_NEAR(m.axis_ratios[0], 0.6, kTol);
    CHECK_NEAR(m.axis_ratios[1], 0.8, kTol);
    CHECK_NEAR(m.axis_ratios[2], 0.0, kTol);
    // 距離 50 は 25 + 25 の境界なので、総時間 1.0 s で最高速度 100 に到達する
    CHECK_NEAR(m.lead_profile.total_time, 1.0, kTol);

    float pos[3], vel[3];
    bool finished = motion_multi_axis_step(&m, 0.5f, pos, vel); // t=0.5: 合成距離 25、合成速度 100
    CHECK(!finished);
    CHECK_NEAR(pos[0], 15.0, kTol);
    CHECK_NEAR(pos[1], 20.0, kTol);
    CHECK_NEAR(pos[2], 0.0, kTol);
    CHECK_NEAR(vel[0], 60.0, kTol);
    CHECK_NEAR(vel[1], 80.0, kTol);
    CHECK_NEAR(vel[2], 0.0, kTol);
    finished = motion_multi_axis_step(&m, 0.5f, pos, vel); // t=1.0: 全軸が同時に目標へ到達
    CHECK(finished);
    CHECK_NEAR(pos[0], 30.0, kTol);
    CHECK_NEAR(pos[1], 40.0, kTol);
    CHECK_NEAR(vel[0], 0.0, kTol);
    CHECK_NEAR(vel[1], 0.0, kTol);

    // 細かい刻みで走らせ、軌跡が直線 (4x = 3y) 上にあり、合成速度が最高速度以下であること
    REQUIRE(motion_multi_axis_plan(&m, 3, &c, start, target));
    double max_speed = 0;
    int steps = 0;
    bool done = false;
    while (!done && steps < 5000) {
        done = motion_multi_axis_step(&m, 0.001f, pos, vel);
        CHECK_NEAR(4.0 * pos[0] - 3.0 * pos[1], 0.0, 1e-3);
        max_speed = std::fmax(max_speed, std::sqrt((double)vel[0] * vel[0] + (double)vel[1] * vel[1]));
        ++steps;
    }
    CHECK(done);
    CHECK(steps >= 999 && steps <= 1001); // 総時間 1.0 s を 1 ms 刻み (float の時刻加算の誤差を許容)
    CHECK(max_speed <= 100.0 + 1e-2);
    CHECK_NEAR(max_speed, 100.0, 0.5);
}

TEST_CASE(multi_axis_handles_offsets_negative_axes_and_four_axes) {
    const motion_constraints_t c{10, 20, 20, 0};
    // 開始位置のオフセットと、負方向の軸: (10,-10) -> (13,-6)  Δ=(3,4)、距離 5
    motion_multi_axis_t m{};
    const float s2[2] = {10, -10}, t2[2] = {13, -6};
    REQUIRE(motion_multi_axis_plan(&m, 2, &c, s2, t2));
    float pos[2], vel[2];
    bool done = false;
    int guard = 0;
    while (!done && guard++ < 100000)
        done = motion_multi_axis_step(&m, 0.01f, pos, vel);
    CHECK(done);
    CHECK_NEAR(pos[0], 13.0, kTol);
    CHECK_NEAR(pos[1], -6.0, kTol);

    const float s2b[2] = {5, 5}, t2b[2] = {2, 1}; // 両軸とも負方向: Δ=(-3,-4)
    REQUIRE(motion_multi_axis_plan(&m, 2, &c, s2b, t2b));
    CHECK_NEAR(m.axis_ratios[0], -0.6, kTol);
    CHECK_NEAR(m.axis_ratios[1], -0.8, kTol);
    motion_multi_axis_step(&m, 0.5f, pos, vel);
    CHECK(vel[0] < 0.0f);
    CHECK(vel[1] < 0.0f);
    CHECK_NEAR(vel[1] / vel[0], 4.0 / 3.0, 1e-3);

    // 4 軸 (最大): Δ=(1,2,2,4) -> 距離 5 (ratios 0.2, 0.4, 0.4, 0.8)
    const float s4[4] = {0, 0, 0, 0}, t4[4] = {1, 2, 2, 4};
    motion_multi_axis_t m4{};
    REQUIRE(motion_multi_axis_plan(&m4, 4, &c, s4, t4));
    CHECK_NEAR(m4.axis_ratios[0], 1.0 / std::sqrt(25.0), kTol);
    float p4[4], v4[4];
    done = false;
    guard = 0;
    while (!done && guard++ < 100000)
        done = motion_multi_axis_step(&m4, 0.01f, p4, v4);
    CHECK(done);
    for (int i = 0; i < 4; ++i)
        CHECK_NEAR(p4[i], t4[i], kTol);
}

TEST_CASE(multi_axis_zero_length_and_invalid_arguments) {
    const motion_constraints_t c{100, 200, 200, 0};
    motion_multi_axis_t m{};
    const float a[3] = {1, 2, 3};
    float pos[3], vel[3];
    // 移動量 0 は即座に完了
    REQUIRE(motion_multi_axis_plan(&m, 3, &c, a, a));
    CHECK(motion_multi_axis_step(&m, 0.01f, pos, vel));
    CHECK_NEAR(vel[0], 0.0, kTol);

    const float b[3] = {2, 3, 4};
    CHECK(!motion_multi_axis_plan(nullptr, 3, &c, a, b));
    CHECK(!motion_multi_axis_plan(&m, 0, &c, a, b));
    CHECK(!motion_multi_axis_plan(&m, MOTION_MAX_AXES + 1, &c, a, b));
    CHECK(!motion_multi_axis_plan(&m, 3, nullptr, a, b));
    CHECK(!motion_multi_axis_plan(&m, 3, &c, nullptr, b));
    CHECK(!motion_multi_axis_plan(&m, 3, &c, a, nullptr));
    const motion_constraints_t bad{0, 200, 200, 0};
    CHECK(!motion_multi_axis_plan(&m, 3, &bad, a, b));
    // step の引数が不正なら「完了」を返す
    REQUIRE(motion_multi_axis_plan(&m, 3, &c, a, b));
    CHECK(motion_multi_axis_step(nullptr, 0.01f, pos, vel));
    CHECK(motion_multi_axis_step(&m, 0.01f, nullptr, vel));
    CHECK(motion_multi_axis_step(&m, 0.01f, pos, nullptr));
}

EMBED_TEST_MAIN()
