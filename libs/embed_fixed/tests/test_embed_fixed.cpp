#include "embed_fixed/embed_fixed.h"
#include "embed_fixed/embed_fixed.hpp"
#include "embed_test/embed_test.hpp"
#include <climits>
#include <cmath>
TEST_CASE(saturation_and_math) {
    CHECK_EQ(embed_q31_add(INT32_MAX, 1), INT32_MAX);
    CHECK_EQ(embed_q31_sub(INT32_MIN, 1), INT32_MIN);
    CHECK_EQ(embed_q31_mul(INT32_MIN, INT32_MIN), INT32_MAX);
    CHECK_EQ(embed_q31_div(INT32_MIN, -1), INT32_MAX);
    CHECK_EQ(embed_q15_mul(INT16_MIN, INT16_MIN), INT16_MAX);
    CHECK_EQ(embed_q15_div(0, 0), 0);
    CHECK_EQ(embed_q31_sqrt(536870912), 1073741824);
    CHECK_EQ(embed_q31_sqrt(-1), 0);
    for (int i = -1000; i < 1000; ++i) {
        int32_t phase = (int32_t)((int64_t)i * 2147483648 / 1000);
        CHECK_NEAR(embed_q31_sin(phase) / 2147483648.0, std::sin(i * 3.141592653589793 / 1000),
                   0.000006);
        CHECK_NEAR(embed_q31_cos(phase) / 2147483648.0, std::cos(i * 3.141592653589793 / 1000),
                   0.000006);
    }
    CHECK_EQ(embed_q31_atan2(1, 0), 1073741824);
    CHECK_EQ(embed_q31_atan2(0, -1), INT32_MIN);
    for (int x = -10; x <= 10; ++x)
        for (int y = -10; y <= 10; ++y)
            if (x && y)
                CHECK_NEAR(embed_q31_atan2(y, x) / 2147483648.0,
                           std::atan2((double)y, (double)x) / 3.141592653589793, 0.000006);
    embed_fixed::LowPass<embed_fixed::Q15> filter;
    filter.alpha = embed_fixed::Q15::from_raw(16384);
    CHECK_EQ(filter.update(embed_fixed::Q15::from_raw(16384)).raw, 8192);
}

TEST_CASE(generic_pid_and_q15_math) {
    embed_fixed::PID<embed_fixed::Q31> pid;
    pid.kp = embed_fixed::Q31::from_raw(1073741824);
    pid.low = embed_fixed::Q31::from_raw(INT32_MIN);
    pid.high = embed_fixed::Q31::from_raw(INT32_MAX);
    CHECK_EQ(pid.update(embed_fixed::Q31::from_raw(1073741824), {},
                        embed_fixed::Q31::from_raw(1073741824))
                 .raw,
             536870912);
    embed_fixed::PID<float> floating;
    floating.kp = 0.5f;
    floating.low = -1;
    floating.high = 1;
    CHECK_NEAR(floating.update(0.5f, 0.0f, 0.5f), 0.25, 0.000001);
    for (int i = 0; i <= INT16_MAX; i += 127) {
        CHECK_NEAR(embed_q15_sqrt((int16_t)i) / 32768.0, std::sqrt(i / 32768.0), 1.0 / 32768);
        CHECK_NEAR(embed_q15_sin((int16_t)i) / 32768.0, std::sin(i * 3.141592653589793 / 32768),
                   0.00004);
    }
}
EMBED_TEST_MAIN()
