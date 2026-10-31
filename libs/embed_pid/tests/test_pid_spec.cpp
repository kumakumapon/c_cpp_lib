// 制御則の数式・出力制限・不感帯・微分フィルタ・アンチワインドアップ・バンプレス転送・
// Ziegler-Nichols の表・一次遅れプラントの閉ループ応答を、手計算した値で固定するテスト。
#include "embed_test/embed_test.hpp"
#include <cmath>
#include <initializer_list>

#include "embed_pid/embed_pid.hpp"
#include "embed_pid/pid_controller.h"
#include "embed_pid/pid_types.h"

namespace {
constexpr float kTol = 1e-4f;

pid_config_t make_cfg(float kp, float ki, float kd, float dt) {
    pid_config_t c;
    pid_get_default_config(&c);
    c.kp = kp;
    c.ki = ki;
    c.kd = kd;
    c.dt = dt;
    c.anti_windup = PID_ANTI_WINDUP_NONE;
    return c;
}
} // namespace

TEST_CASE(defaults_and_init) {
    pid_config_t c;
    pid_get_default_config(&c);
    CHECK_NEAR(c.kp, 1.0f, 0.0f);
    CHECK_NEAR(c.ki, 0.0f, 0.0f);
    CHECK_NEAR(c.kd, 0.0f, 0.0f);
    CHECK_NEAR(c.dt, 0.01f, 0.0f);
    CHECK_NEAR(c.out_min, -100.0f, 0.0f);
    CHECK_NEAR(c.out_max, 100.0f, 0.0f);
    CHECK_NEAR(c.deadband, 0.0f, 0.0f);
    CHECK_NEAR(c.tau_d, 0.0f, 0.0f);
    CHECK_NEAR(c.kt, 0.0f, 0.0f);
    CHECK_EQ((int)c.mode, (int)PID_MODE_POSITIONAL);
    CHECK_EQ((int)c.anti_windup, (int)PID_ANTI_WINDUP_CLAMPING);

    embed_pid_t pid;
    pid_init(&pid, nullptr); // NULL なら既定値
    CHECK_NEAR(pid.config.kp, 1.0f, 0.0f);
    CHECK(pid.is_auto);
    CHECK_NEAR(pid.integral, 0.0f, 0.0f);
    CHECK_NEAR(pid.saturated_output, 0.0f, 0.0f);

    // NULL を渡しても落ちない
    pid_get_default_config(nullptr);
    pid_init(nullptr, &c);
    pid_reset(nullptr);
    pid_set_config(nullptr, &c);
    pid_set_manual_output(nullptr, 1.0f);
    pid_set_mode(nullptr, true, 0.0f);
    CHECK_NEAR(pid_compute(nullptr, 1.0f, 0.0f), 0.0f, 0.0f);

    // pid_set_config は設定だけ差し替え、NULL なら無視
    pid_config_t other = make_cfg(5.0f, 0.0f, 0.0f, 0.1f);
    pid_set_config(&pid, &other);
    CHECK_NEAR(pid.config.kp, 5.0f, 0.0f);
    pid_set_config(&pid, nullptr);
    CHECK_NEAR(pid.config.kp, 5.0f, 0.0f);
}

TEST_CASE(reset_clears_all_state) {
    pid_config_t c = make_cfg(2.0f, 1.0f, 0.5f, 0.1f);
    embed_pid_t pid;
    pid_init(&pid, &c);
    pid_compute(&pid, 10.0f, 0.0f);
    pid_compute(&pid, 10.0f, 3.0f);
    pid_reset(&pid);
    CHECK_NEAR(pid.integral, 0.0f, 0.0f);
    CHECK_NEAR(pid.prev_error, 0.0f, 0.0f);
    CHECK_NEAR(pid.prev2_error, 0.0f, 0.0f);
    CHECK_NEAR(pid.prev_pv, 0.0f, 0.0f);
    CHECK_NEAR(pid.prev_d_term, 0.0f, 0.0f);
    CHECK_NEAR(pid.prev_output, 0.0f, 0.0f);
    CHECK_NEAR(pid.p_term, 0.0f, 0.0f);
    CHECK_NEAR(pid.i_term, 0.0f, 0.0f);
    CHECK_NEAR(pid.d_term, 0.0f, 0.0f);
    CHECK_NEAR(pid.output, 0.0f, 0.0f);
    CHECK_NEAR(pid.saturated_output, 0.0f, 0.0f);
    // 初期化直後と同じ応答に戻る
    CHECK_NEAR(pid_compute(&pid, 10.0f, 0.0f), 71.0f, kTol);
}

// 位置型: u = Kp e + Ki Σ e dt + Kd (e - e_prev)/dt を e = 10, 5, 5, 0 で手計算
TEST_CASE(positional_sequence) {
    pid_config_t c = make_cfg(2.0f, 1.0f, 0.5f, 0.1f);
    embed_pid_t pid;
    pid_init(&pid, &c);

    CHECK_NEAR(pid_compute(&pid, 10.0f, 0.0f), 71.0f, kTol); // 20 + 1 + 50
    CHECK_NEAR(pid.p_term, 20.0f, kTol);
    CHECK_NEAR(pid.i_term, 1.0f, kTol);
    CHECK_NEAR(pid.d_term, 50.0f, kTol);

    CHECK_NEAR(pid_compute(&pid, 10.0f, 5.0f), -13.5f, kTol); // 10 + 1.5 - 25
    CHECK_NEAR(pid.p_term, 10.0f, kTol);
    CHECK_NEAR(pid.i_term, 1.5f, kTol);
    CHECK_NEAR(pid.d_term, -25.0f, kTol);

    CHECK_NEAR(pid_compute(&pid, 10.0f, 5.0f), 12.0f, kTol); // 10 + 2.0 + 0
    CHECK_NEAR(pid.d_term, 0.0f, kTol);

    CHECK_NEAR(pid_compute(&pid, 5.0f, 5.0f), -23.0f, kTol); // 0 + 2.0 - 25
    CHECK_NEAR(pid.p_term, 0.0f, kTol);
    CHECK_NEAR(pid.i_term, 2.0f, kTol);
    CHECK_NEAR(pid.d_term, -25.0f, kTol);
}

// 速度型: u[k] = u[k-1] + Kp Δe + Ki e dt + Kd/dt Δ²e。制限に掛からない間は位置型と同じ値になる
TEST_CASE(velocity_sequence) {
    pid_config_t c = make_cfg(2.0f, 1.0f, 0.5f, 0.1f);
    c.mode = PID_MODE_VELOCITY;
    embed_pid_t pid;
    pid_init(&pid, &c);

    CHECK_NEAR(pid_compute(&pid, 10.0f, 0.0f), 71.0f, kTol);
    CHECK_NEAR(pid.p_term, 20.0f, kTol); // Kp * (10 - 0)
    CHECK_NEAR(pid.i_term, 1.0f, kTol);  // Ki * 10 * 0.1
    CHECK_NEAR(pid.d_term, 50.0f, kTol); // Kd/dt * (10 - 0 + 0)

    CHECK_NEAR(pid_compute(&pid, 10.0f, 5.0f), -13.5f, kTol);
    CHECK_NEAR(pid.p_term, -10.0f, kTol); // Kp * (5 - 10)
    CHECK_NEAR(pid.i_term, 0.5f, kTol);   // Ki * 5 * 0.1
    CHECK_NEAR(pid.d_term, -75.0f, kTol); // 5 * (5 - 20 + 0)

    CHECK_NEAR(pid_compute(&pid, 10.0f, 5.0f), 12.0f, kTol);
    CHECK_NEAR(pid.d_term, 25.0f, kTol); // 5 * (5 - 10 + 10)

    CHECK_NEAR(pid_compute(&pid, 5.0f, 5.0f), -23.0f, kTol);
}

TEST_CASE(velocity_saturation_feeds_back_clamped_output) {
    pid_config_t c = make_cfg(1.0f, 0.0f, 0.0f, 0.1f);
    c.mode = PID_MODE_VELOCITY;
    c.out_min = -5.0f;
    c.out_max = 5.0f;
    embed_pid_t pid;
    pid_init(&pid, &c);

    CHECK_NEAR(pid_compute(&pid, 4.0f, 0.0f), 4.0f, kTol);  // 0 + 1*(4-0)
    CHECK_NEAR(pid_compute(&pid, 8.0f, 0.0f), 5.0f, kTol);  // 4 + 4 = 8 -> 5
    CHECK_NEAR(pid.output, 8.0f, kTol);                     // 飽和前の値は残る
    CHECK_NEAR(pid.saturated_output, 5.0f, kTol);
    // 次の増分は飽和後の 5 に加える (飽和前の 8 ではない): 5 + (8-8)*1 = 5, e を 2 に下げると 5 - 6 = -1
    CHECK_NEAR(pid_compute(&pid, 2.0f, 0.0f), -1.0f, kTol);
    CHECK_NEAR(pid_compute(&pid, -9.0f, 0.0f), -5.0f, kTol); // 下限
}

// 微分先行型 (PI-D): 目標値の変化では微分が出ず、PV の変化の符号反転で出る。P/I は偏差のまま
TEST_CASE(derivative_on_pv) {
    pid_config_t c = make_cfg(2.0f, 0.0f, 1.0f, 0.1f);
    c.mode = PID_MODE_DERIVATIVE_ON_PV;
    embed_pid_t pid;
    pid_init(&pid, &c);

    CHECK_NEAR(pid_compute(&pid, 0.0f, 0.0f), 0.0f, kTol);
    // 目標値だけ 0 -> 10: D は 0、P = 2*10
    CHECK_NEAR(pid_compute(&pid, 10.0f, 0.0f), 20.0f, kTol);
    CHECK_NEAR(pid.d_term, 0.0f, kTol);
    // PV 0 -> 1: D = -Kd * 1 / 0.1 = -10、P = 2*9
    CHECK_NEAR(pid_compute(&pid, 10.0f, 1.0f), 8.0f, kTol);
    CHECK_NEAR(pid.d_term, -10.0f, kTol);
    // PV 1 -> 3: D = -20
    CHECK_NEAR(pid_compute(&pid, 10.0f, 3.0f), 2.0f * 7.0f - 20.0f, kTol);
    CHECK_NEAR(pid.d_term, -20.0f, kTol);
    // PV が下がれば D は正
    CHECK_NEAR(pid_compute(&pid, 10.0f, 2.0f), 2.0f * 8.0f + 10.0f, kTol);
    CHECK_NEAR(pid.d_term, 10.0f, kTol);

    // 同じ目標値の急変でも、偏差微分型 (位置型) は微分キックを出す
    pid_config_t p = make_cfg(2.0f, 0.0f, 1.0f, 0.1f);
    embed_pid_t pos;
    pid_init(&pos, &p);
    pid_compute(&pos, 0.0f, 0.0f);
    pid_compute(&pos, 10.0f, 0.0f);
    CHECK_NEAR(pos.d_term, 100.0f, kTol); // Kd * 10 / 0.1
}

// D_filtered = (tau * D_prev + dt * D_raw) / (tau + dt)
TEST_CASE(derivative_filter) {
    pid_config_t c = make_cfg(0.0f, 0.0f, 1.0f, 0.1f);
    c.tau_d = 0.2f;
    embed_pid_t pid;
    pid_init(&pid, &c);

    // e: 0 -> 1 (D_raw = 10) -> 1 (D_raw = 0) -> 1
    pid_compute(&pid, 1.0f, 0.0f);
    CHECK_NEAR(pid.d_term, 0.1f * 10.0f / 0.3f, kTol); // 3.3333
    pid_compute(&pid, 1.0f, 0.0f);
    CHECK_NEAR(pid.d_term, 0.2f * (0.1f * 10.0f / 0.3f) / 0.3f, 1e-3f); // 2.2222
    pid_compute(&pid, 1.0f, 0.0f);
    CHECK_NEAR(pid.d_term, 0.2f * (0.2f * (0.1f * 10.0f / 0.3f) / 0.3f) / 0.3f, 1e-3f); // 1.4815

    // フィルタ無効 (tau_d == 0) のときは生の微分がそのまま出る
    pid_config_t n = make_cfg(0.0f, 0.0f, 1.0f, 0.1f);
    embed_pid_t raw;
    pid_init(&raw, &n);
    pid_compute(&raw, 1.0f, 0.0f);
    CHECK_NEAR(raw.d_term, 10.0f, kTol);
}

TEST_CASE(deadband_boundary) {
    pid_config_t c = make_cfg(2.0f, 10.0f, 0.0f, 0.1f);
    c.deadband = 0.5f;
    embed_pid_t pid;
    pid_init(&pid, &c);

    // |e| == deadband は不感帯の内側 (出力 0、積分も増えない)
    CHECK_NEAR(pid_compute(&pid, 0.5f, 0.0f), 0.0f, kTol);
    CHECK_NEAR(pid_compute(&pid, -0.5f, 0.0f), 0.0f, kTol);
    CHECK_NEAR(pid.integral, 0.0f, kTol);
    // 超えると偏差そのものが効く (不感帯幅を引かない)
    CHECK_NEAR(pid_compute(&pid, 0.75f, 0.0f), 2.0f * 0.75f + 10.0f * 0.75f * 0.1f, kTol);
    pid_reset(&pid);
    CHECK_NEAR(pid_compute(&pid, -0.75f, 0.0f), 2.0f * -0.75f + 10.0f * -0.75f * 0.1f, kTol);

    // 不感帯 0 なら微小な偏差でも効く
    pid_config_t z = make_cfg(2.0f, 0.0f, 0.0f, 0.1f);
    embed_pid_t zp;
    pid_init(&zp, &z);
    CHECK_NEAR(pid_compute(&zp, 0.001f, 0.0f), 0.002f, 1e-6f);
}

TEST_CASE(deadband_zeroes_error_history) {
    // 不感帯内の偏差は 0 として履歴に入る: 直後に不感帯を出た偏差の微分は e - 0
    pid_config_t c = make_cfg(0.0f, 0.0f, 1.0f, 0.1f);
    c.deadband = 1.0f;
    embed_pid_t pid;
    pid_init(&pid, &c);
    pid_compute(&pid, 0.8f, 0.0f);
    CHECK_NEAR(pid.prev_error, 0.0f, 0.0f);
    pid_compute(&pid, 2.0f, 0.0f);
    CHECK_NEAR(pid.d_term, 20.0f, kTol);
}

TEST_CASE(output_limits) {
    pid_config_t c = make_cfg(2.0f, 0.0f, 0.0f, 0.1f);
    c.out_min = -3.0f;
    c.out_max = 5.0f;
    embed_pid_t pid;
    pid_init(&pid, &c);

    CHECK_NEAR(pid_compute(&pid, 10.0f, 0.0f), 5.0f, 0.0f);
    CHECK_NEAR(pid.output, 20.0f, kTol); // 飽和前の値
    CHECK_NEAR(pid_compute(&pid, -10.0f, 0.0f), -3.0f, 0.0f);
    CHECK_NEAR(pid.output, -20.0f, kTol);
    // ちょうど境界の値はそのまま
    CHECK_NEAR(pid_compute(&pid, 2.5f, 0.0f), 5.0f, 0.0f);
    CHECK_NEAR(pid_compute(&pid, -1.5f, 0.0f), -3.0f, 0.0f);
    // 範囲内
    CHECK_NEAR(pid_compute(&pid, 1.0f, 0.0f), 2.0f, kTol);
    CHECK_NEAR(pid_compute(&pid, -1.0f, 0.0f), -2.0f, kTol);
}

TEST_CASE(dt_fallback) {
    // dt <= 1e-6 は 1 ms として扱う
    for (float dt : {0.0f, -1.0f, 0.000001f}) {
        pid_config_t c = make_cfg(0.0f, 1000.0f, 0.0f, dt);
        embed_pid_t pid;
        pid_init(&pid, &c);
        CHECK_NEAR(pid_compute(&pid, 1.0f, 0.0f), 1.0f, kTol); // Ki * e * 0.001
    }
    // 1e-6 を超える dt はそのまま使う
    pid_config_t c = make_cfg(0.0f, 1000.0f, 0.0f, 0.000002f);
    embed_pid_t pid;
    pid_init(&pid, &c);
    CHECK_NEAR(pid_compute(&pid, 1.0f, 0.0f), 0.002f, 1e-6f);
}

// ---- アンチワインドアップ ----
// Kp=1, Ki=10, dt=0.1, e=1: 積分は 1 ステップあたり 1 ずつ増え、出力 = 1 + 積分 (上限 5)
TEST_CASE(anti_windup_none_keeps_integrating) {
    pid_config_t c = make_cfg(1.0f, 10.0f, 0.0f, 0.1f);
    c.out_max = 5.0f;
    embed_pid_t pid;
    pid_init(&pid, &c);
    for (int k = 1; k <= 8; ++k) pid_compute(&pid, 1.0f, 0.0f);
    CHECK_NEAR(pid.integral, 8.0f, kTol);
    CHECK_NEAR(pid.saturated_output, 5.0f, kTol);
    CHECK_NEAR(pid.output, 9.0f, kTol); // 1 + 8
    CHECK_NEAR(pid.i_term, 8.0f, kTol);
}

TEST_CASE(anti_windup_clamping) {
    pid_config_t c = make_cfg(1.0f, 10.0f, 0.0f, 0.1f);
    c.out_max = 5.0f;
    c.out_min = -5.0f;
    c.anti_windup = PID_ANTI_WINDUP_CLAMPING;
    embed_pid_t pid;
    pid_init(&pid, &c);

    // k=1..3: 出力 2, 3, 4 (飽和なし)、積分 1, 2, 3
    for (int k = 1; k <= 3; ++k) pid_compute(&pid, 1.0f, 0.0f);
    CHECK_NEAR(pid.integral, 3.0f, kTol);
    // k=4: 出力がちょうど上限 (5) かつ e>0 なので積分は 3 のまま
    CHECK_NEAR(pid_compute(&pid, 1.0f, 0.0f), 5.0f, kTol);
    CHECK_NEAR(pid.integral, 3.0f, kTol);
    for (int k = 5; k <= 20; ++k) pid_compute(&pid, 1.0f, 0.0f);
    CHECK_NEAR(pid.integral, 3.0f, kTol);

    // 偏差が負に転じれば直ちに積分が戻る (e=-1: 出力 -1 + (3 - 1) = 1)
    CHECK_NEAR(pid_compute(&pid, -1.0f, 0.0f), 1.0f, kTol);
    CHECK_NEAR(pid.integral, 2.0f, kTol);

    // 下側も対称: 出力がちょうど下限で e<0 なら積分は止まる
    pid_reset(&pid);
    for (int k = 1; k <= 20; ++k) pid_compute(&pid, -1.0f, 0.0f);
    CHECK_NEAR(pid.integral, -3.0f, kTol);
    CHECK_NEAR(pid.saturated_output, -5.0f, kTol);
    CHECK_NEAR(pid_compute(&pid, 1.0f, 0.0f), 1.0f + (-3.0f + 1.0f), kTol);
}

TEST_CASE(anti_windup_clamping_only_blocks_same_direction) {
    // 上限に張り付いていても、偏差が逆向き (e<0) の間は積分を更新する。
    // Kp=10 で P だけで上限を超え、e = -0.1 では P=-1 なので出力は上限に張り付かないため
    // 「出力>=上限 かつ e<0」は積分更新、の組合せを積分初期値で作る
    pid_config_t c = make_cfg(0.0f, 10.0f, 0.0f, 0.1f);
    c.out_max = 5.0f;
    c.out_min = -5.0f;
    c.anti_windup = PID_ANTI_WINDUP_CLAMPING;
    embed_pid_t pid;
    pid_init(&pid, &c);
    pid.integral = 8.0f; // 既に飽和側に積分が溜まっている
    CHECK_NEAR(pid_compute(&pid, -1.0f, 0.0f), 5.0f, kTol); // 8 - 1 = 7 -> 5
    CHECK_NEAR(pid.integral, 7.0f, kTol);                    // e<0 なので積分は進む (戻る)
    pid.integral = -8.0f;
    CHECK_NEAR(pid_compute(&pid, 1.0f, 0.0f), -5.0f, kTol);
    CHECK_NEAR(pid.integral, -7.0f, kTol);
}

TEST_CASE(anti_windup_back_calculation) {
    // I += (Ki e + Kt (u_sat - u)) dt。Kp=1, Ki=10, dt=0.1, Kt=2, e=1, 上限 5
    pid_config_t c = make_cfg(1.0f, 10.0f, 0.0f, 0.1f);
    c.out_max = 5.0f;
    c.out_min = -5.0f;
    c.anti_windup = PID_ANTI_WINDUP_BACK_CALC;
    c.kt = 2.0f;
    embed_pid_t pid;
    pid_init(&pid, &c);

    for (int k = 1; k <= 4; ++k) pid_compute(&pid, 1.0f, 0.0f); // 積分 1,2,3,4 (出力 2,3,4,5)
    CHECK_NEAR(pid.integral, 4.0f, kTol);
    pid_compute(&pid, 1.0f, 0.0f);                // 出力 6 -> 5、差 -1
    CHECK_NEAR(pid.integral, 4.0f + (10.0f - 2.0f) * 0.1f, kTol); // 4.8
    pid_compute(&pid, 1.0f, 0.0f);                // 出力 1 + 4.8 + 1 = 6.8、差 -1.8
    CHECK_NEAR(pid.integral, 4.8f + (10.0f - 2.0f * 1.8f) * 0.1f, kTol); // 5.44
    CHECK_NEAR(pid.i_term, pid.integral, 0.0f);
}

TEST_CASE(anti_windup_back_calculation_default_gain) {
    // Kt が 0 以下なら 1/dt: 補正量が Ki e dt と打ち消し合う位置で積分が止まる
    for (float kt : {0.0f, -3.0f}) {
        pid_config_t c = make_cfg(1.0f, 10.0f, 0.0f, 0.1f);
        c.out_max = 5.0f;
        c.out_min = -5.0f;
        c.anti_windup = PID_ANTI_WINDUP_BACK_CALC;
        c.kt = kt;
        embed_pid_t pid;
        pid_init(&pid, &c);
        for (int k = 1; k <= 30; ++k) pid_compute(&pid, 1.0f, 0.0f);
        CHECK_NEAR(pid.integral, 4.0f, kTol);
        CHECK_NEAR(pid.saturated_output, 5.0f, kTol);
        // 飽和が解けると直ちに応答する: e=-1 で -1 + (4 - 1) = 2
        CHECK_NEAR(pid_compute(&pid, -1.0f, 0.0f), 2.0f, kTol);
    }
}

// ---- 手動 / 自動切替 ----
TEST_CASE(manual_output_and_hold) {
    pid_config_t c = make_cfg(1.0f, 1.0f, 0.0f, 0.1f);
    c.out_min = -10.0f;
    c.out_max = 10.0f;
    embed_pid_t pid;
    pid_init(&pid, &c);

    pid_set_manual_output(&pid, 25.0f);
    CHECK_NEAR(pid.saturated_output, 10.0f, 0.0f); // 範囲に収める
    CHECK_NEAR(pid.output, 10.0f, 0.0f);
    CHECK_NEAR(pid.prev_output, 10.0f, 0.0f);
    pid_set_manual_output(&pid, -25.0f);
    CHECK_NEAR(pid.saturated_output, -10.0f, 0.0f);
    pid_set_manual_output(&pid, 3.0f);
    CHECK_NEAR(pid.saturated_output, 3.0f, 0.0f);

    // 手動中は偏差に関係なく固定出力、内部状態は動かない
    pid_set_mode(&pid, false, 0.0f);
    CHECK(!pid.is_auto);
    CHECK_NEAR(pid_compute(&pid, 100.0f, 0.0f), 3.0f, 0.0f);
    CHECK_NEAR(pid.integral, 0.0f, 0.0f);

    // 自動中に手動へ切り替えた場合は、直前の自動出力を保持する
    embed_pid_t held;
    pid_init(&held, &c);
    float u = pid_compute(&held, 2.0f, 0.0f);
    pid_set_mode(&held, false, 0.0f);
    CHECK_NEAR(pid_compute(&held, 50.0f, 0.0f), u, 0.0f);
}

TEST_CASE(bumpless_transfer_positional) {
    pid_config_t c = make_cfg(2.0f, 1.0f, 0.5f, 0.1f);
    embed_pid_t pid;
    pid_init(&pid, &c);
    pid_set_mode(&pid, false, 0.0f);
    pid_set_manual_output(&pid, 40.0f);

    // PV を目標値に合わせて自動へ: 偏差 0 なので出力は手動値のまま (許容誤差 1e-3)
    pid_set_mode(&pid, true, 50.0f);
    CHECK(pid.is_auto);
    CHECK_NEAR(pid.integral, 40.0f, kTol);
    CHECK_NEAR(pid_compute(&pid, 50.0f, 50.0f), 40.0f, 1e-3f);
    CHECK_NEAR(pid_compute(&pid, 50.0f, 50.0f), 40.0f, 1e-3f);
}

TEST_CASE(bumpless_transfer_derivative_on_pv_has_no_kick) {
    pid_config_t c = make_cfg(1.0f, 0.0f, 1.0f, 0.1f);
    c.mode = PID_MODE_DERIVATIVE_ON_PV;
    embed_pid_t pid;
    pid_init(&pid, &c);
    pid_set_mode(&pid, false, 0.0f);
    pid_set_manual_output(&pid, 20.0f);
    pid_set_mode(&pid, true, 50.0f); // PV の履歴を現在値で初期化する
    CHECK_NEAR(pid.prev_pv, 50.0f, 0.0f);
    CHECK_NEAR(pid_compute(&pid, 50.0f, 50.0f), 20.0f, 1e-3f);
    CHECK_NEAR(pid.d_term, 0.0f, kTol);
}

TEST_CASE(bumpless_transfer_resets_error_history) {
    pid_config_t c = make_cfg(1.0f, 0.0f, 1.0f, 0.1f);
    embed_pid_t pid;
    pid_init(&pid, &c);
    pid_compute(&pid, 10.0f, 0.0f);
    pid_compute(&pid, 20.0f, 0.0f);
    pid_set_mode(&pid, false, 0.0f);
    pid_set_manual_output(&pid, 5.0f);
    pid_set_mode(&pid, true, 20.0f);
    CHECK_NEAR(pid.prev_error, 0.0f, 0.0f);
    CHECK_NEAR(pid.prev2_error, 0.0f, 0.0f);
    CHECK_NEAR(pid.prev_d_term, 0.0f, 0.0f);
    CHECK_NEAR(pid_compute(&pid, 20.0f, 20.0f), 5.0f, 1e-3f);
}

TEST_CASE(bumpless_transfer_velocity) {
    pid_config_t c = make_cfg(2.0f, 1.0f, 0.5f, 0.1f);
    c.mode = PID_MODE_VELOCITY;
    embed_pid_t pid;
    pid_init(&pid, &c);
    pid_set_mode(&pid, false, 0.0f);
    pid_set_manual_output(&pid, 30.0f);
    pid_set_mode(&pid, true, 10.0f);
    CHECK_NEAR(pid_compute(&pid, 10.0f, 10.0f), 30.0f, 1e-3f);
}

TEST_CASE(set_mode_same_state_keeps_integral) {
    pid_config_t c = make_cfg(1.0f, 10.0f, 0.0f, 0.1f);
    embed_pid_t pid;
    pid_init(&pid, &c);
    pid_compute(&pid, 1.0f, 0.0f);
    pid_compute(&pid, 1.0f, 0.0f);
    CHECK_NEAR(pid.integral, 2.0f, kTol);
    pid_set_mode(&pid, true, 7.0f); // 自動 -> 自動 は何もしない
    CHECK_NEAR(pid.integral, 2.0f, kTol);
    CHECK_NEAR(pid.prev_pv, 0.0f, 0.0f);
    pid_set_mode(&pid, false, 7.0f); // 自動 -> 手動 も状態は変えない
    CHECK_NEAR(pid.integral, 2.0f, kTol);
    CHECK_NEAR(pid.prev_pv, 0.0f, 0.0f);
}

// ---- Ziegler-Nichols ----
TEST_CASE(ziegler_nichols_table) {
    const float ku = 10.0f, tu = 2.0f;
    struct Row {
        zn_tuning_rule_t rule;
        float kp, ki, kd;
    };
    // Kp, Ti, Td の表 (Ki = Kp/Ti, Kd = Kp*Td)
    const Row rows[] = {
        {ZN_TUNING_P_ONLY, 5.0f, 0.0f, 0.0f},                       // 0.5 Ku
        {ZN_TUNING_PI, 4.5f, 4.5f / (2.0f / 1.2f), 0.0f},           // 0.45 Ku, Tu/1.2
        {ZN_TUNING_PID_CLASSIC, 6.0f, 6.0f / 1.0f, 6.0f * 0.25f},   // 0.6 Ku, Tu/2, Tu/8
        {ZN_TUNING_PID_SOME_OVERSHOOT, 3.3f, 3.3f / 1.0f, 3.3f * (2.0f / 3.0f)},
        {ZN_TUNING_PID_NO_OVERSHOOT, 2.0f, 2.0f / 1.0f, 2.0f * (2.0f / 3.0f)},
    };
    for (const Row &r : rows) {
        float kp = -1, ki = -1, kd = -1;
        pid_tune_ziegler_nichols(ku, tu, r.rule, &kp, &ki, &kd);
        CHECK_NEAR(kp, r.kp, 1e-4f);
        CHECK_NEAR(ki, r.ki, 1e-4f);
        CHECK_NEAR(kd, r.kd, 1e-4f);
    }

    // Ku, Tu に比例/反比例する (別の値でも表のとおり)
    float kp, ki, kd;
    pid_tune_ziegler_nichols(4.0f, 0.5f, ZN_TUNING_PID_CLASSIC, &kp, &ki, &kd);
    CHECK_NEAR(kp, 2.4f, 1e-5f);
    CHECK_NEAR(ki, 2.4f / 0.25f, 1e-4f);
    CHECK_NEAR(kd, 2.4f * 0.0625f, 1e-5f);
}

TEST_CASE(ziegler_nichols_invalid_inputs) {
    const float bad[][2] = {{0.0f, 1.0f}, {-1.0f, 1.0f}, {1.0f, 0.0f}, {1.0f, -1.0f}};
    for (const auto &b : bad) {
        float kp = 9, ki = 9, kd = 9;
        pid_tune_ziegler_nichols(b[0], b[1], ZN_TUNING_PID_CLASSIC, &kp, &ki, &kd);
        CHECK_NEAR(kp, 0.0f, 0.0f);
        CHECK_NEAR(ki, 0.0f, 0.0f);
        CHECK_NEAR(kd, 0.0f, 0.0f);
    }
    // 未定義の種別は全ゲイン 0
    float kp = 9, ki = 9, kd = 9;
    pid_tune_ziegler_nichols(10.0f, 2.0f, (zn_tuning_rule_t)99, &kp, &ki, &kd);
    CHECK_NEAR(kp, 0.0f, 0.0f);
    CHECK_NEAR(ki, 0.0f, 0.0f);
    CHECK_NEAR(kd, 0.0f, 0.0f);
    // 出力先が NULL でも落ちない / 無効入力でも NULL を許す
    pid_tune_ziegler_nichols(10.0f, 2.0f, ZN_TUNING_PI, nullptr, nullptr, nullptr);
    pid_tune_ziegler_nichols(0.0f, 2.0f, ZN_TUNING_PI, nullptr, nullptr, nullptr);
    float only_kp = 0;
    pid_tune_ziegler_nichols(10.0f, 2.0f, ZN_TUNING_PI, &only_kp, nullptr, nullptr);
    CHECK_NEAR(only_kp, 4.5f, 1e-5f);
}

// ---- 一次遅れプラントの閉ループ ----
namespace {
// dy/dt = (K u - y)/T を零次ホールドで厳密に離散化したプラント
struct Plant {
    double k, t, dt, y;
    double step(double u) {
        const double a = std::exp(-dt / t);
        y = a * y + (1.0 - a) * k * u;
        return y;
    }
};
} // namespace

TEST_CASE(closed_loop_p_only_steady_state_offset) {
    // P 制御の定常値: y = K Kp / (1 + K Kp) * sp
    const double K = 2.0, T = 1.0, dt = 0.01;
    pid_config_t c = make_cfg(3.0f, 0.0f, 0.0f, (float)dt);
    embed_pid_t pid;
    pid_init(&pid, &c);
    Plant plant{K, T, dt, 0.0};
    for (int i = 0; i < 2000; ++i) {
        plant.step(pid_compute(&pid, 1.0f, (float)plant.y));
    }
    CHECK_NEAR((float)plant.y, (float)(K * 3.0 / (1.0 + K * 3.0)), 1e-4f); // 6/7
}

TEST_CASE(closed_loop_pi_pole_zero_cancellation) {
    // Ti = T の PI は極零相殺で、閉ループが時定数 T/(K Kp) = 0.5 s の一次遅れになる:
    // y(t) = 1 - exp(-t/0.5)。オーバーシュートなし
    const double K = 1.0, T = 1.0, dt = 0.001;
    pid_config_t c = make_cfg(2.0f, 2.0f, 0.0f, (float)dt); // Ki = Kp / Ti
    c.out_min = -1000.0f;
    c.out_max = 1000.0f;
    embed_pid_t pid;
    pid_init(&pid, &c);
    Plant plant{K, T, dt, 0.0};
    double max_y = 0.0;
    double y_half = 0.0, y_one = 0.0;
    for (int i = 1; i <= 5000; ++i) {
        plant.step(pid_compute(&pid, 1.0f, (float)plant.y));
        if (plant.y > max_y) max_y = plant.y;
        if (i == 500) y_half = plant.y;
        if (i == 1000) y_one = plant.y;
    }
    CHECK_NEAR((float)y_half, (float)(1.0 - std::exp(-0.5 / 0.5)), 0.01f); // 0.6321
    CHECK_NEAR((float)y_one, (float)(1.0 - std::exp(-1.0 / 0.5)), 0.01f);  // 0.8647
    CHECK(max_y <= 1.0 + 1e-3);
    CHECK_NEAR((float)plant.y, 1.0f, 1e-3f); // 積分動作で偏差が消える
}

TEST_CASE(closed_loop_windup_recovery) {
    // 出力上限 5 のプラント (K=1) に、到達できない目標 100 を 5 s 与えてから 2 に戻し、3 s 後の y を見る。
    // アンチワインドアップなしでは溜まった積分で出力が上限に張り付いたままで 2 に戻れず、
    // あり (クランプ / 逆算) はほぼ目標に収まる
    auto run = [](pid_anti_windup_t aw) {
        const double dt = 0.01;
        pid_config_t c = make_cfg(2.0f, 4.0f, 0.0f, (float)dt);
        c.out_min = -5.0f;
        c.out_max = 5.0f;
        c.anti_windup = aw;
        c.kt = 10.0f;
        embed_pid_t pid;
        pid_init(&pid, &c);
        Plant plant{1.0, 1.0, dt, 0.0};
        for (int i = 0; i < 500; ++i) plant.step(pid_compute(&pid, 100.0f, (float)plant.y));
        for (int i = 0; i < 300; ++i) plant.step(pid_compute(&pid, 2.0f, (float)plant.y));
        return plant.y;
    };
    const double none = run(PID_ANTI_WINDUP_NONE);
    const double clamp = run(PID_ANTI_WINDUP_CLAMPING);
    const double back = run(PID_ANTI_WINDUP_BACK_CALC);
    CHECK(none > 4.0);
    CHECK_NEAR((float)clamp, 2.0f, 0.1f);
    CHECK_NEAR((float)back, 2.0f, 0.1f);
}

// ---- C++ ラッパ ----
TEST_CASE(cpp_controller_matches_c_api) {
    embed_pid::Controller ctl(2.0f, 1.0f, 0.5f, 0.1f);
    ctl.set_anti_windup(PID_ANTI_WINDUP_NONE);
    pid_config_t c = make_cfg(2.0f, 1.0f, 0.5f, 0.1f);
    embed_pid_t ref;
    pid_init(&ref, &c);
    const float pv[] = {0.0f, 5.0f, 5.0f, 8.0f, 12.0f};
    for (float v : pv) {
        CHECK_NEAR(ctl.update(10.0f, v), pid_compute(&ref, 10.0f, v), 0.0f);
        CHECK_NEAR(ctl.p_term(), ref.p_term, 0.0f);
        CHECK_NEAR(ctl.i_term(), ref.i_term, 0.0f);
        CHECK_NEAR(ctl.d_term(), ref.d_term, 0.0f);
        CHECK_NEAR(ctl.last_output(), ref.saturated_output, 0.0f);
    }
    CHECK(ctl.raw_handle() != nullptr);
}

TEST_CASE(cpp_controller_setters) {
    embed_pid::Controller ctl; // 既定: kp=1, ki=0, kd=0, dt=0.01
    CHECK_NEAR(ctl.config().kp, 1.0f, 0.0f);
    CHECK_NEAR(ctl.config().ki, 0.0f, 0.0f);
    CHECK_NEAR(ctl.config().kd, 0.0f, 0.0f);
    CHECK_NEAR(ctl.config().dt, 0.01f, 0.0f);
    CHECK(ctl.is_auto());

    ctl.set_gains(3.0f, 4.0f, 5.0f)
        .set_dt(0.2f)
        .set_output_limits(-7.0f, 9.0f)
        .set_deadband(0.25f)
        .set_filter_time_constant(0.3f)
        .set_mode(PID_MODE_VELOCITY)
        .set_anti_windup(PID_ANTI_WINDUP_BACK_CALC, 6.0f);
    const pid_config_t &c = ctl.config();
    CHECK_NEAR(c.kp, 3.0f, 0.0f);
    CHECK_NEAR(c.ki, 4.0f, 0.0f);
    CHECK_NEAR(c.kd, 5.0f, 0.0f);
    CHECK_NEAR(c.dt, 0.2f, 0.0f);
    CHECK_NEAR(c.out_min, -7.0f, 0.0f);
    CHECK_NEAR(c.out_max, 9.0f, 0.0f);
    CHECK_NEAR(c.deadband, 0.25f, 0.0f);
    CHECK_NEAR(c.tau_d, 0.3f, 0.0f);
    CHECK_EQ((int)c.mode, (int)PID_MODE_VELOCITY);
    CHECK_EQ((int)c.anti_windup, (int)PID_ANTI_WINDUP_BACK_CALC);
    CHECK_NEAR(c.kt, 6.0f, 0.0f);

    // 手動 / 自動 / リセット
    ctl.set_manual_output(100.0f); // 上限 9 に収める
    ctl.set_auto_mode(false);
    CHECK(!ctl.is_auto());
    CHECK_NEAR(ctl.update(50.0f, 0.0f), 9.0f, 0.0f);
    CHECK_NEAR(ctl.last_output(), 9.0f, 0.0f);
    ctl.set_auto_mode(true, 1.0f);
    CHECK(ctl.is_auto());
    ctl.reset();
    CHECK_NEAR(ctl.last_output(), 0.0f, 0.0f);

    // コンストラクタ (設定構造体) は設定をそのまま取り込む
    pid_config_t src = make_cfg(1.5f, 0.0f, 0.0f, 0.05f);
    embed_pid::Controller from_cfg(src);
    CHECK_NEAR(from_cfg.update(2.0f, 0.0f), 3.0f, kTol);
}

EMBED_TEST_MAIN()
