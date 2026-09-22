#include <iostream>
#include <vector>
#include "embed_test/embed_test.hpp"
#include <cmath>

#include "embed_pid/pid_types.h"
#include "embed_pid/pid_controller.h"
#include "embed_pid/embed_pid.hpp"

#define EPSILON 0.0001f

static bool approx_eq(float a, float b, float eps = 0.001f) {
    return fabsf(a - b) <= eps;
}

TEST_CASE(test_basic_pid_terms) {
    std::cout << "# [Test] Running Basic P, I, D term tests..." << std::endl;

    embed_pid_t pid;
    pid_config_t cfg;
    pid_get_default_config(&cfg);
    cfg.kp = 2.0f;
    cfg.ki = 1.0f;
    cfg.kd = 0.5f;
    cfg.dt = 0.1f; // 100ms
    cfg.out_min = -100.0f;
    cfg.out_max = 100.0f;

    pid_init(&pid, &cfg);

    // Step 1: SP=10, PV=0 -> Error=10
    // P = 2.0 * 10 = 20.0
    // I = 1.0 * 10 * 0.1 = 1.0
    // D = 0.5 * (10 - 0) / 0.1 = 50.0
    // Total = 20 + 1 + 50 = 71.0
    float out1 = pid_compute(&pid, 10.0f, 0.0f);
    CHECK(approx_eq(out1, 71.0f));
    CHECK(approx_eq(pid.p_term, 20.0f));
    CHECK(approx_eq(pid.i_term, 1.0f));
    CHECK(approx_eq(pid.d_term, 50.0f));

    // Step 2: SP=10, PV=5 -> Error=5
    // P = 2.0 * 5 = 10.0
    // I = 1.0 + (1.0 * 5 * 0.1) = 1.5
    // D = 0.5 * (5 - 10) / 0.1 = -25.0
    // Total = 10 + 1.5 - 25 = -13.5
    float out2 = pid_compute(&pid, 10.0f, 5.0f);
    CHECK(approx_eq(out2, -13.5f));
    CHECK(approx_eq(pid.p_term, 10.0f));
    CHECK(approx_eq(pid.i_term, 1.5f));
    CHECK(approx_eq(pid.d_term, -25.0f));

    std::cout << "# [Test] Basic P, I, D terms passed!" << std::endl;
}

TEST_CASE(test_anti_windup_and_saturation) {
    std::cout << "# [Test] Running Anti-Windup & Saturation tests..." << std::endl;

    embed_pid_t pid;
    pid_config_t cfg;
    pid_get_default_config(&cfg);
    cfg.kp = 10.0f;
    cfg.ki = 5.0f;
    cfg.kd = 0.0f;
    cfg.dt = 0.1f;
    cfg.out_min = 0.0f;
    cfg.out_max = 50.0f;
    cfg.anti_windup = PID_ANTI_WINDUP_CLAMPING;

    pid_init(&pid, &cfg);

    // 継続的な大誤差で出力を50.0に飽和させる
    for (int i = 0; i < 20; ++i) {
        float out = pid_compute(&pid, 100.0f, 0.0f);
        CHECK(out == 50.0f);
    }

    // 飽和クランプが効いているため、積分項が無限に増大していないことを確認
    // P項だけで 10 * 100 = 1000 > 50 なので、積分項はクランプされて0のまま
    CHECK(approx_eq(pid.i_term, 0.0f));

    std::cout << "# [Test] Anti-Windup & Saturation passed!" << std::endl;
}

TEST_CASE(test_velocity_and_derivative_modes) {
    std::cout << "# [Test] Running Velocity & Derivative-on-PV tests..." << std::endl;

    // 1. 微分先行型 (PI-D) のステップ応答テスト
    embed_pid_t pid_deriv_pv;
    pid_config_t cfg;
    pid_get_default_config(&cfg);
    cfg.kp = 1.0f;
    cfg.ki = 0.0f;
    cfg.kd = 2.0f;
    cfg.dt = 0.1f;
    cfg.mode = PID_MODE_DERIVATIVE_ON_PV;

    pid_init(&pid_deriv_pv, &cfg);

    // SP が 0 -> 100 に急変しても、PVが動かなければ D項は 0 (キックなし)
    float out_no_kick = pid_compute(&pid_deriv_pv, 100.0f, 0.0f);
    CHECK(approx_eq(pid_deriv_pv.d_term, 0.0f));
    CHECK(approx_eq(out_no_kick, 100.0f)); // P項のみ

    // 2. 速度型 (増分型) PID テスト
    embed_pid_t pid_vel;
    cfg.mode = PID_MODE_VELOCITY;
    cfg.ki = 1.0f;
    pid_init(&pid_vel, &cfg);

    float vel_out1 = pid_compute(&pid_vel, 10.0f, 0.0f);
    CHECK(vel_out1 > 0.0f);

    std::cout << "# [Test] Velocity & Derivative-on-PV passed!" << std::endl;
}

TEST_CASE(test_bumpless_transfer) {
    std::cout << "# [Test] Running Bumpless Transfer tests..." << std::endl;

    embed_pid_t pid;
    pid_config_t cfg;
    pid_get_default_config(&cfg);
    cfg.kp = 2.0f;
    cfg.ki = 1.0f;
    cfg.kd = 0.0f;
    cfg.dt = 0.1f;
    pid_init(&pid, &cfg);

    // 手動モードで 35.0% 出力を維持
    pid_set_mode(&pid, false, 20.0f);
    pid_set_manual_output(&pid, 35.0f);
    CHECK(approx_eq(pid_compute(&pid, 50.0f, 20.0f), 35.0f));

    // 自動モードへ切り替え (バプレス転送)
    pid_set_mode(&pid, true, 20.0f);

    // 切り替え直後の出力が急変せずスムーズに追従することを確認
    float auto_out = pid_compute(&pid, 50.0f, 20.0f);
    CHECK(auto_out >= 35.0f); // ショックなく継続

    std::cout << "# [Test] Bumpless Transfer passed!" << std::endl;
}

TEST_CASE(test_ziegler_nichols_tuning) {
    std::cout << "# [Test] Running Ziegler-Nichols tuning tests..." << std::endl;

    float ku = 10.0f;
    float tu = 2.0f;
    float kp = 0, ki = 0, kd = 0;

    pid_tune_ziegler_nichols(ku, tu, ZN_TUNING_PID_CLASSIC, &kp, &ki, &kd);
    CHECK(approx_eq(kp, 6.0f)); // 0.6 * Ku = 6.0
    CHECK(approx_eq(ki, 6.0f)); // Kp / (Tu / 2.0) = 6.0 / 1.0 = 6.0
    CHECK(approx_eq(kd, 1.5f)); // Kp * (Tu / 8.0) = 6.0 * 0.25 = 1.5

    std::cout << "# [Test] Ziegler-Nichols tuning passed!" << std::endl;
}

TEST_CASE(test_cpp_controller_class) {
    std::cout << "# [Test] Running C++ Controller class tests..." << std::endl;

    embed_pid::Controller pid(1.5f, 0.5f, 0.1f, 0.05f);
    pid.set_output_limits(-50.0f, 50.0f)
       .set_deadband(0.1f)
       .set_anti_windup(PID_ANTI_WINDUP_CLAMPING);

    float sp = 100.0f;
    float pv = 0.0f;

    // 1次遅れ系の簡易シミュレーション
    for (int step = 0; step < 50; ++step) {
        float u = pid.update(sp, pv);
        pv += (u * 0.1f) - (pv * 0.05f); // プラントモデル
    }

    // 目標値に収束していることを確認
    CHECK(pv > 50.0f);

    std::cout << "# [Test] C++ Controller class passed!" << std::endl;
}

EMBED_TEST_MAIN()
