// Q15 / Q31 の飽和・丸め (0 方向への切り捨て)・除算の端点、平方根・三角関数の象限境界と誤差上限、
// LowPass / PID アダプタの手計算値を固定するテスト。
#include "embed_fixed/embed_fixed.h"
#include "embed_fixed/embed_fixed.hpp"
#include "embed_test/embed_test.hpp"
#include <climits>
#include <cmath>
#include <initializer_list>

namespace {
constexpr double kPi = 3.141592653589793;
double q31(int32_t v) { return v / 2147483648.0; }
double q15(int16_t v) { return v / 32768.0; }
} // namespace

// ---- 加減算 (飽和) ----
TEST_CASE(add_sub_saturate_at_both_ends) {
    CHECK_EQ(embed_q15_add(100, 200), 300);
    CHECK_EQ(embed_q15_add(-100, -200), -300);
    CHECK_EQ(embed_q15_add(INT16_MAX, 0), INT16_MAX);
    CHECK_EQ(embed_q15_add(INT16_MAX, 1), INT16_MAX);
    CHECK_EQ(embed_q15_add(INT16_MAX, INT16_MAX), INT16_MAX);
    CHECK_EQ(embed_q15_add(INT16_MIN, -1), INT16_MIN);
    CHECK_EQ(embed_q15_add(INT16_MIN, INT16_MIN), INT16_MIN);
    CHECK_EQ(embed_q15_add(INT16_MIN, INT16_MAX), -1);
    CHECK_EQ(embed_q15_sub(300, 200), 100);
    CHECK_EQ(embed_q15_sub(0, INT16_MIN), INT16_MAX); // 0 - (-32768) = 32768 -> 飽和
    CHECK_EQ(embed_q15_sub(INT16_MAX, -1), INT16_MAX);
    CHECK_EQ(embed_q15_sub(INT16_MIN, 1), INT16_MIN);
    CHECK_EQ(embed_q15_sub(INT16_MIN, INT16_MIN), 0);
    CHECK_EQ(embed_q15_sub(INT16_MAX, INT16_MAX), 0);
    CHECK_EQ(embed_q15_sub(-1, INT16_MAX), INT16_MIN);

    CHECK_EQ(embed_q31_add(100, 200), 300);
    CHECK_EQ(embed_q31_add(INT32_MAX, 1), INT32_MAX);
    CHECK_EQ(embed_q31_add(INT32_MAX, INT32_MAX), INT32_MAX);
    CHECK_EQ(embed_q31_add(INT32_MIN, -1), INT32_MIN);
    CHECK_EQ(embed_q31_add(INT32_MIN, INT32_MAX), -1);
    CHECK_EQ(embed_q31_sub(0, INT32_MIN), INT32_MAX);
    CHECK_EQ(embed_q31_sub(INT32_MIN, 1), INT32_MIN);
    CHECK_EQ(embed_q31_sub(INT32_MAX, -1), INT32_MAX);
    CHECK_EQ(embed_q31_sub(7, 9), -2);
    CHECK_EQ(embed_q31_sub(INT32_MAX, INT32_MAX), 0);
}

// ---- 乗算 (0 方向への切り捨て、飽和) ----
TEST_CASE(mul_rounds_toward_zero_and_saturates) {
    CHECK_EQ(embed_q15_mul(16384, 16384), 8192);    // 0.5 * 0.5 = 0.25
    CHECK_EQ(embed_q15_mul(-16384, 16384), -8192);
    CHECK_EQ(embed_q15_mul(-16384, -16384), 8192);
    CHECK_EQ(embed_q15_mul(0, 12345), 0);
    CHECK_EQ(embed_q15_mul(INT16_MAX, INT16_MAX), 32766);
    CHECK_EQ(embed_q15_mul(INT16_MAX, INT16_MIN), -32767);
    CHECK_EQ(embed_q15_mul(INT16_MIN, INT16_MIN), INT16_MAX); // +1.0 は表せない -> 飽和
    // 切り捨ては 0 方向: 1 * 1 = 1/32768 -> 0, (-1) * 1 -> 0 (床ではなく -1 にならない)
    CHECK_EQ(embed_q15_mul(1, 1), 0);
    CHECK_EQ(embed_q15_mul(-1, 1), 0);
    CHECK_EQ(embed_q15_mul(3, 16384), 1);  // 1.5 -> 1
    CHECK_EQ(embed_q15_mul(-3, 16384), -1); // -1.5 -> -1
    CHECK_EQ(embed_q15_mul(32767, 1), 0);

    CHECK_EQ(embed_q31_mul(1073741824, 1073741824), 536870912);
    CHECK_EQ(embed_q31_mul(-1073741824, 1073741824), -536870912);
    CHECK_EQ(embed_q31_mul(INT32_MIN, INT32_MIN), INT32_MAX);
    CHECK_EQ(embed_q31_mul(INT32_MAX, INT32_MIN), -INT32_MAX);
    CHECK_EQ(embed_q31_mul(INT32_MAX, INT32_MAX), INT32_MAX - 1);
    CHECK_EQ(embed_q31_mul(1, 1), 0);
    CHECK_EQ(embed_q31_mul(-1, 1), 0);
    CHECK_EQ(embed_q31_mul(3, 1073741824), 1);
    CHECK_EQ(embed_q31_mul(-3, 1073741824), -1);
    CHECK_EQ(embed_q31_mul(0, INT32_MAX), 0);
}

// ---- 除算 ----
TEST_CASE(div_saturates_and_handles_zero) {
    CHECK_EQ(embed_q15_div(8192, 16384), 16384);   // 0.25 / 0.5 = 0.5
    CHECK_EQ(embed_q15_div(-8192, 16384), -16384);
    CHECK_EQ(embed_q15_div(8192, -16384), -16384);
    CHECK_EQ(embed_q15_div(-8192, -16384), 16384);
    CHECK_EQ(embed_q15_div(16384, 16384), INT16_MAX); // 1.0 は表せない -> 飽和
    CHECK_EQ(embed_q15_div(INT16_MIN, INT16_MAX), INT16_MIN); // ≈ -1.00003 -> 飽和
    CHECK_EQ(embed_q15_div(INT16_MIN, -1), INT16_MAX);
    CHECK_EQ(embed_q15_div(1, 3), 10922);  // 32768 / 3 = 10922.67 -> 0 方向へ
    CHECK_EQ(embed_q15_div(-1, 3), -10922);
    CHECK_EQ(embed_q15_div(0, 5), 0);
    // 0 除算は符号で飽和し、0/0 は 0
    CHECK_EQ(embed_q15_div(5, 0), INT16_MAX);
    CHECK_EQ(embed_q15_div(-5, 0), INT16_MIN);
    CHECK_EQ(embed_q15_div(0, 0), 0);

    CHECK_EQ(embed_q31_div(536870912, 1073741824), 1073741824);
    CHECK_EQ(embed_q31_div(-536870912, 1073741824), -1073741824);
    CHECK_EQ(embed_q31_div(536870912, -1073741824), -1073741824);
    CHECK_EQ(embed_q31_div(1073741824, 1073741824), INT32_MAX);
    CHECK_EQ(embed_q31_div(INT32_MIN, -1), INT32_MAX);
    CHECK_EQ(embed_q31_div(INT32_MIN, INT32_MAX), INT32_MIN);
    CHECK_EQ(embed_q31_div(1, 3), 715827882);
    CHECK_EQ(embed_q31_div(-1, 3), -715827882);
    CHECK_EQ(embed_q31_div(0, 5), 0);
    CHECK_EQ(embed_q31_div(5, 0), INT32_MAX);
    CHECK_EQ(embed_q31_div(-5, 0), INT32_MIN);
    CHECK_EQ(embed_q31_div(0, 0), 0);
}

// ---- 平方根 ----
TEST_CASE(sqrt_values_and_bounds) {
    CHECK_EQ(embed_q15_sqrt(0), 0);
    CHECK_EQ(embed_q15_sqrt(8192), 16384);  // sqrt(0.25) = 0.5
    CHECK_EQ(embed_q15_sqrt(16384), 23170); // sqrt(0.5) = 0.70710... * 32768 = 23170.47
    CHECK_EQ(embed_q15_sqrt(32767), 32767);
    CHECK_EQ(embed_q15_sqrt(1), 181);       // sqrt(32768) = 181.02
    CHECK_EQ(embed_q15_sqrt(-1), 0);        // 負は 0
    CHECK_EQ(embed_q15_sqrt(INT16_MIN), 0);

    CHECK_EQ(embed_q31_sqrt(0), 0);
    CHECK_EQ(embed_q31_sqrt(536870912), 1073741824);
    CHECK_EQ(embed_q31_sqrt(INT32_MAX), 2147483647);
    CHECK_EQ(embed_q31_sqrt(1), 46340);     // sqrt(2^31) = 46340.95 -> 切り捨て
    CHECK_EQ(embed_q31_sqrt(-1), 0);
    CHECK_EQ(embed_q31_sqrt(INT32_MIN), 0);
}

TEST_CASE(sqrt_is_floor_of_the_true_root_everywhere) {
    // 結果 r は r^2 <= x * 2^k < (r + 1)^2 を満たす (切り捨てた厳密解)
    for (int x = 0; x <= INT16_MAX; x += 7) {
        const int64_t r = embed_q15_sqrt((int16_t)x);
        const int64_t n = (int64_t)x * 32768;
        CHECK(r * r <= n);
        CHECK((r + 1) * (r + 1) > n);
    }
    uint32_t rng = 7;
    for (int i = 0; i < 4000; ++i) {
        rng = rng * 1664525u + 1013904223u;
        const int32_t x = (int32_t)(rng >> 1);
        const int64_t r = embed_q31_sqrt(x);
        const int64_t n = (int64_t)x * 2147483648LL;
        CHECK(r * r <= n);
        CHECK((r + 1) * (r + 1) > n);
    }
}

// ---- 三角関数 ----
TEST_CASE(sin_cos_quadrant_points) {
    // 角度は π で正規化: 16384 = π/2, 32767 ≈ π, -32768 = -π
    CHECK_EQ(embed_q15_sin(0), 0);
    CHECK_EQ(embed_q15_sin(16384), 32767);
    CHECK_EQ(embed_q15_sin(-16384), -32767);
    CHECK_EQ(embed_q15_sin(INT16_MIN), 0);
    CHECK_EQ(embed_q15_sin(8192), 23170);     // sin(π/4)
    CHECK_EQ(embed_q15_sin(-8192), -23170);
    CHECK_EQ(embed_q15_sin(24576), 23170);    // sin(3π/4)
    CHECK_EQ(embed_q15_sin(-24576), -23170);
    CHECK_EQ(embed_q15_cos(0), 32767);
    CHECK_EQ(embed_q15_cos(16384), 0);
    CHECK_EQ(embed_q15_cos(-16384), 0);
    CHECK_EQ(embed_q15_cos(INT16_MIN), -32767);
    CHECK_EQ(embed_q15_cos(8192), 23170);
    CHECK_EQ(embed_q15_cos(24576), -23170);   // cos(3π/4)
    CHECK_EQ(embed_q15_cos(-24576), -23170);

    CHECK_EQ(embed_q31_sin(0), 0);
    CHECK_EQ(embed_q31_sin(1073741824), INT32_MAX);
    CHECK_EQ(embed_q31_sin(-1073741824), -INT32_MAX);
    CHECK_EQ(embed_q31_sin(INT32_MIN), 0);
    CHECK_EQ(embed_q31_cos(0), INT32_MAX);
    CHECK_EQ(embed_q31_cos(1073741824), 0);
    CHECK_EQ(embed_q31_cos(-1073741824), 0);
    CHECK_EQ(embed_q31_cos(INT32_MIN), -INT32_MAX);
}

TEST_CASE(sin_symmetries_are_exact) {
    uint32_t rng = 99;
    for (int i = 0; i < 3000; ++i) {
        rng = rng * 1664525u + 1013904223u;
        const int32_t x = (int32_t)(rng & 0x7fffffffu) - 0x3fffffff; // 約 ±π/2 (半周)
        CHECK_EQ(embed_q31_sin(-x), -embed_q31_sin(x));              // 奇関数
        CHECK_EQ(embed_q31_sin((int32_t)(0x80000000u - (uint32_t)x)), embed_q31_sin(x)); // sin(π - x)
        CHECK_EQ(embed_q31_cos(x), embed_q31_sin((int32_t)((uint32_t)x + 0x40000000u)));  // cos = sin(x + π/2)
        CHECK_EQ(embed_q31_cos(-x), embed_q31_cos(x));                                    // 偶関数
    }
}

TEST_CASE(sin_cos_accuracy_and_monotonicity) {
    double worst = 0.0;
    int32_t prev = INT32_MIN;
    for (int64_t p = -(1LL << 30); p <= (1LL << 30); p += 1LL << 18) { // -π/2 .. π/2 を 4096 点
        const int32_t s = embed_q31_sin((int32_t)p);
        CHECK(s >= prev); // 単調増加
        prev = s;
        worst = std::fmax(worst, std::fabs(q31(s) - std::sin(p * kPi / 2147483648.0)));
    }
    CHECK(worst < 6e-6);
    worst = 0.0;
    for (int64_t p = -(1LL << 31); p < (1LL << 31); p += 1LL << 19) {
        const double a = p * kPi / 2147483648.0;
        worst = std::fmax(worst, std::fabs(q31(embed_q31_cos((int32_t)p)) - std::cos(a)));
        worst = std::fmax(worst, std::fabs(q31(embed_q31_sin((int32_t)p)) - std::sin(a)));
    }
    CHECK(worst < 6e-6);
    // 単位円: sin^2 + cos^2 ≈ 1
    for (int32_t p = INT32_MIN; p < INT32_MAX - (1 << 22); p += 1 << 22) {
        const double s = q31(embed_q31_sin(p)), c = q31(embed_q31_cos(p));
        CHECK_NEAR(s * s + c * c, 1.0, 2e-5);
    }
    // Q15 は Q31 を 16 ビット右へ丸めた値で、誤差は 1 LSB 以内
    for (int x = INT16_MIN; x <= INT16_MAX; x += 61) {
        CHECK_NEAR(q15(embed_q15_sin((int16_t)x)), std::sin(x * kPi / 32768.0), 1.0 / 32768 + 6e-6);
        CHECK_NEAR(q15(embed_q15_cos((int16_t)x)), std::cos(x * kPi / 32768.0), 1.0 / 32768 + 6e-6);
    }
}

TEST_CASE(atan2_axes_diagonals_and_quadrants) {
    // 角度は π で正規化 (π/4 = 0x20000000)
    CHECK_EQ(embed_q31_atan2(0, 0), 0);
    CHECK_EQ(embed_q31_atan2(0, 5), 0);
    CHECK_EQ(embed_q31_atan2(5, 0), 1073741824);   // π/2
    CHECK_EQ(embed_q31_atan2(-5, 0), -1073741824); // -π/2
    CHECK_EQ(embed_q31_atan2(0, -5), INT32_MIN);   // ±π は同じ位相 (-1) で表す
    CHECK_EQ(embed_q31_atan2(-0, -5), INT32_MIN);
    CHECK_EQ(embed_q31_atan2(7, 7), 536870912);    // π/4
    CHECK_EQ(embed_q31_atan2(7, -7), 1610612736);  // 3π/4
    CHECK_EQ(embed_q31_atan2(-7, -7), -1610612736);
    CHECK_EQ(embed_q31_atan2(-7, 7), -536870912);
    CHECK_EQ(embed_q31_atan2(INT32_MAX, INT32_MAX), 536870912);
    CHECK_EQ(embed_q31_atan2(INT32_MIN, INT32_MIN), -1610612736);
    CHECK_EQ(embed_q31_atan2(INT32_MIN, 0), -1073741824);
    CHECK_EQ(embed_q31_atan2(0, INT32_MIN), INT32_MIN);

    CHECK_EQ(embed_q15_atan2(0, 0), 0);
    CHECK_EQ(embed_q15_atan2(0, 100), 0);
    CHECK_EQ(embed_q15_atan2(100, 0), 16384);
    CHECK_EQ(embed_q15_atan2(-100, 0), -16384);
    CHECK_EQ(embed_q15_atan2(0, -100), INT16_MIN);
    CHECK_EQ(embed_q15_atan2(100, 100), 8192);
    CHECK_EQ(embed_q15_atan2(100, -100), 24576);
    CHECK_EQ(embed_q15_atan2(-100, -100), -24576);
    CHECK_EQ(embed_q15_atan2(-100, 100), -8192);
}

TEST_CASE(atan2_is_scale_invariant_and_accurate) {
    // 比が同じなら同じ角度
    for (int k : {2, 3, 100, 1000}) {
        CHECK_EQ(embed_q31_atan2(3 * k, 5 * k), embed_q31_atan2(3, 5));
        CHECK_EQ(embed_q31_atan2(-3 * k, 5 * k), embed_q31_atan2(-3, 5));
        CHECK_EQ(embed_q31_atan2(3 * k, -5 * k), embed_q31_atan2(3, -5));
    }
    double worst = 0.0;
    uint32_t rng = 31337;
    for (int i = 0; i < 20000; ++i) {
        rng = rng * 1664525u + 1013904223u;
        const int shift = (int)(rng >> 27) % 24; // 振幅を広く振る
        int32_t y = (int32_t)(rng * 2654435761u) >> shift;
        rng = rng * 1664525u + 1013904223u;
        int32_t x = (int32_t)(rng) >> ((int)(rng >> 27) % 24);
        if (x == 0 && y == 0) continue;
        double exact = std::atan2((double)y, (double)x) / kPi;
        double got = q31(embed_q31_atan2(y, x));
        double d = std::fabs(got - exact);
        if (d > 1.0) d = 2.0 - d; // ±π の折り返し
        worst = std::fmax(worst, d);
    }
    CHECK(worst < 6e-6);
    // 象限をまたぐ連続性: y = 0 の近傍で符号が切り替わる (比は 1/65536 刻みで量子化される)
    CHECK(embed_q31_atan2(1, 1000) > 0);
    CHECK(embed_q31_atan2(-1, 1000) < 0);
    CHECK(embed_q31_atan2(1, -1000) > 1073741824);
    CHECK(embed_q31_atan2(-1, -1000) < -1073741824);
    CHECK_EQ(embed_q31_atan2(1, 1000000), 0); // 比が 1/65536 未満は 0 に丸まる
}

// ---- アダプタ ----
TEST_CASE(q_class_operators_map_to_saturating_functions) {
    using embed_fixed::Q15;
    using embed_fixed::Q31;
    CHECK_EQ((Q15::from_raw(INT16_MAX) + Q15::from_raw(1)).raw, INT16_MAX);
    CHECK_EQ((Q15::from_raw(100) - Q15::from_raw(300)).raw, -200);
    CHECK_EQ((Q15::from_raw(16384) * Q15::from_raw(16384)).raw, 8192);
    CHECK_EQ((Q15::from_raw(8192) / Q15::from_raw(16384)).raw, 16384);
    CHECK((Q15::from_raw(-1) < Q15::from_raw(0)));
    CHECK(!(Q15::from_raw(0) < Q15::from_raw(0)));
    CHECK_EQ(Q15().raw, 0);
    CHECK_EQ((Q31::from_raw(INT32_MIN) - Q31::from_raw(1)).raw, INT32_MIN);
    CHECK_EQ((Q31::from_raw(1073741824) * Q31::from_raw(1073741824)).raw, 536870912);
    CHECK_EQ((Q31::from_raw(536870912) / Q31::from_raw(0)).raw, INT32_MAX);
}

TEST_CASE(low_pass_sequence) {
    using embed_fixed::Q15;
    embed_fixed::LowPass<Q15> f;
    f.alpha = Q15::from_raw(16384); // 0.5
    CHECK_EQ(f.update(Q15::from_raw(16384)).raw, 8192);  // 0 + 0.5 * (0.5 - 0)
    CHECK_EQ(f.update(Q15::from_raw(16384)).raw, 12288); // 0.25 + 0.5 * (0.5 - 0.25)
    CHECK_EQ(f.value.raw, 12288);
    CHECK_EQ(f.update(Q15::from_raw(0)).raw, 6144);      // 下がる方向
    // 入力が値と大きく離れていても飽和して発散しない
    embed_fixed::LowPass<Q15> g;
    g.alpha = Q15::from_raw(INT16_MAX);
    g.value = Q15::from_raw(INT16_MIN);
    CHECK_EQ(g.update(Q15::from_raw(INT16_MAX)).raw, INT16_MIN + 32766);

    embed_fixed::LowPass<float> h; // 浮動小数点でも同じ式
    h.alpha = 0.25f;
    CHECK_NEAR(h.update(4.0f), 1.0f, 1e-6);
    CHECK_NEAR(h.update(4.0f), 1.75f, 1e-6);
}

TEST_CASE(pid_float_sequence_and_clamps) {
    embed_fixed::PID<float> pid;
    pid.kp = 2.0f;
    pid.ki = 1.0f;
    pid.kd = 0.05f;
    pid.low = -1.0f;
    pid.high = 1.0f;
    // step1: e = 0.3, I = 0.03, 出力 = 0.6 + 0.03 + 0.05 * (0.3 - 0) / 0.1 = 0.78
    CHECK_NEAR(pid.update(0.3f, 0.0f, 0.1f), 0.78f, 1e-5);
    CHECK_NEAR(pid.integral, 0.03f, 1e-6);
    // step2: e = 0.2, I = 0.05, 出力 = 0.4 + 0.05 + 0.05 * (0.2 - 0.3) / 0.1 = 0.40
    CHECK_NEAR(pid.update(0.3f, 0.1f, 0.1f), 0.40f, 1e-5);
    CHECK_NEAR(pid.previous, 0.2f, 1e-6);

    // 出力の上下限
    embed_fixed::PID<float> sat;
    sat.kp = 10.0f;
    sat.low = -0.5f;
    sat.high = 0.5f;
    CHECK_NEAR(sat.update(1.0f, 0.0f, 0.1f), 0.5f, 0.0f);
    CHECK_NEAR(sat.update(-1.0f, 0.0f, 0.1f), -0.5f, 0.0f);

    // 積分の上下限 (ワインドアップ防止): ki * e * dt = 1 が毎回加わっても 0.5 / -0.5 で止まる
    embed_fixed::PID<float> wind;
    wind.ki = 1.0f;
    wind.low = -0.5f;
    wind.high = 0.5f;
    for (int i = 0; i < 5; ++i) wind.update(1.0f, 0.0f, 1.0f);
    CHECK_NEAR(wind.integral, 0.5f, 0.0f);
    for (int i = 0; i < 5; ++i) wind.update(-1.0f, 0.0f, 1.0f);
    CHECK_NEAR(wind.integral, -0.5f, 0.0f);
}

TEST_CASE(pid_fixed_point_paths) {
    using embed_fixed::PID;
    using embed_fixed::Q15;
    PID<Q15> pid;
    pid.kp = Q15::from_raw(16384); // 0.5
    pid.low = Q15::from_raw(INT16_MIN);
    pid.high = Q15::from_raw(INT16_MAX);
    // e = 0.5, 出力 = 0.5 * 0.5 = 0.25
    CHECK_EQ(pid.update(Q15::from_raw(16384), {}, Q15::from_raw(16384)).raw, 8192);
    CHECK_EQ(pid.previous.raw, 16384);

    // 微分項: (e - prev) / dt が 1.0 を超えると飽和した値に kd を掛ける
    PID<Q15> d;
    d.kd = Q15::from_raw(8192); // 0.25
    d.low = Q15::from_raw(INT16_MIN);
    d.high = Q15::from_raw(INT16_MAX);
    // e = 0.5, prev = 0, dt = 0.5 -> (0.5 / 0.5) = 1.0 -> 32767, 出力 = 0.25 * 32767 = 8191
    CHECK_EQ(d.update(Q15::from_raw(16384), {}, Q15::from_raw(16384)).raw, 8191);

    // 積分項: ki * e * dt = 0.5 * 0.5 * 0.5 = 0.125 ずつ増え、上限で止まる
    PID<Q15> i;
    i.ki = Q15::from_raw(16384);
    i.low = Q15::from_raw(INT16_MIN);
    i.high = Q15::from_raw(6000);
    CHECK_EQ(i.update(Q15::from_raw(16384), {}, Q15::from_raw(16384)).raw, 4096);
    CHECK_EQ(i.integral.raw, 4096);
    CHECK_EQ(i.update(Q15::from_raw(16384), {}, Q15::from_raw(16384)).raw, 6000);
    CHECK_EQ(i.integral.raw, 6000); // 上限でクランプ
    // 下限
    PID<Q15> l;
    l.ki = Q15::from_raw(16384);
    l.low = Q15::from_raw(-6000);
    l.high = Q15::from_raw(INT16_MAX);
    l.update(Q15::from_raw(-16384), {}, Q15::from_raw(16384));
    l.update(Q15::from_raw(-16384), {}, Q15::from_raw(16384));
    CHECK_EQ(l.integral.raw, -6000);
}

EMBED_TEST_MAIN()
