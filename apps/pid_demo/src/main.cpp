#include <iostream>
#include <vector>
#include <iomanip>
#include <cmath>

#include "embed_pid/pid_types.h"
#include "embed_pid/pid_controller.h"
#include "embed_pid/embed_pid.hpp"

// 産業用ヒータ・温度プラントモデル (1次遅れ系: C * dT/dt = P_heater - (T - T_ambient)/R)
class ThermalPlant {
public:
    ThermalPlant(float ambient_temp = 25.0f, float heat_capacity = 10.0f, float thermal_resistance = 2.0f)
        : temp_(ambient_temp), ambient_(ambient_temp), c_(heat_capacity), r_(thermal_resistance) {}

    float update(float heater_power_pct, float dt) {
        // ヒータ出力 0〜100% -> 発熱量 (W)
        float q_in = (heater_power_pct / 100.0f) * 500.0f; // 最大500W
        float q_loss = (temp_ - ambient_) / r_;
        float dt_temp = (q_in - q_loss) / c_ * dt;
        temp_ += dt_temp;
        return temp_;
    }

    float temperature() const { return temp_; }

private:
    float temp_;
    float ambient_;
    float c_;
    float r_;
};

// DCモータ速度プラントモデル (機械時定数 Tm=0.1s, 逆起電力 Kv=0.02)
class MotorPlant {
public:
    MotorPlant(float j = 0.01f, float b = 0.005f, float kt = 0.05f)
        : speed_rpm_(0.0f), j_(j), b_(b), kt_(kt) {}

    float update(float voltage_v, float load_torque_nm, float dt) {
        // 電圧 -12V〜+12V -> モータトルク
        float current = voltage_v / 1.0f; // 電機子抵抗 1.0 ohm
        float motor_torque = kt_ * current;
        float net_torque = motor_torque - load_torque_nm - (b_ * (speed_rpm_ * 0.1047f));
        float accel_rad_s2 = net_torque / j_;
        float speed_rad_s = (speed_rpm_ * 0.1047f) + (accel_rad_s2 * dt);
        if (speed_rad_s < 0.0f) speed_rad_s = 0.0f;
        speed_rpm_ = speed_rad_s / 0.1047f;
        return speed_rpm_;
    }

    float speed_rpm() const { return speed_rpm_; }

private:
    float speed_rpm_;
    float j_;
    float b_;
    float kt_;
};

int main() {
    std::cout << "=====================================================" << std::endl;
    std::cout << " Embedded PID Controller (embed_pid) Demonstration" << std::endl;
    std::cout << "=====================================================" << std::endl;

    // -----------------------------------------------------------------
    // Scenario 1: 産業用ヒータ温度制御 (Anti-Windup 付き位置型PID)
    // -----------------------------------------------------------------
    std::cout << "\n>>> Scenario 1: Thermal Plant Temperature Control (Target: 150 C) <<<" << std::endl;
    embed_pid::Controller temp_pid(4.0f, 0.15f, 8.0f, 0.5f); // Kp=4, Ki=0.15, Kd=8, dt=0.5s
    temp_pid.set_output_limits(0.0f, 100.0f) // ヒータ出力 0〜100%
            .set_anti_windup(PID_ANTI_WINDUP_CLAMPING)
            .set_deadband(0.2f); // 不感帯 0.2℃

    ThermalPlant heater(25.0f); // 室温 25℃
    float target_temp = 150.0f;

    std::cout << "  Time [s] | Target [C] | PV [C]  | Output [%] | P-Term | I-Term | D-Term" << std::endl;
    std::cout << "  ---------+------------+---------+------------+--------+--------+--------" << std::endl;

    for (int step = 0; step <= 40; ++step) {
        float t = step * 0.5f;
        float pv = heater.temperature();
        float u = temp_pid.update(target_temp, pv);
        heater.update(u, 0.5f);

        if (step % 5 == 0) {
            std::cout << "  " << std::setw(8) << std::fixed << std::setprecision(1) << t
                      << " | " << std::setw(10) << target_temp
                      << " | " << std::setw(7) << std::setprecision(2) << pv
                      << " | " << std::setw(10) << std::setprecision(1) << u
                      << " | " << std::setw(6) << temp_pid.p_term()
                      << " | " << std::setw(6) << temp_pid.i_term()
                      << " | " << std::setw(6) << temp_pid.d_term() << std::endl;
        }
    }
    std::cout << "  -> Final steady-state temperature: " << heater.temperature() << " C (Settled smoothly!)" << std::endl;

    // -----------------------------------------------------------------
    // Scenario 2: DCモータ速度制御 (微分先行型 PI-D & 外乱抑制)
    // -----------------------------------------------------------------
    std::cout << "\n>>> Scenario 2: DC Motor Speed Control (Derivative-on-PV, Target: 2000 RPM) <<<" << std::endl;
    embed_pid::Controller motor_pid(0.02f, 0.05f, 0.001f, 0.01f); // dt=10ms
    motor_pid.set_output_limits(-12.0f, 12.0f) // -12V〜+12V
             .set_mode(PID_MODE_DERIVATIVE_ON_PV) // 微分キック防止
             .set_filter_time_constant(0.02f); // 20msローパスフィルタ

    MotorPlant motor;
    float target_rpm = 2000.0f;
    float load_torque = 0.0f;

    std::cout << "  Time [s] | Target RPM | Motor RPM | Voltage [V] | Event / Disturbance" << std::endl;
    std::cout << "  ---------+------------+-----------+-------------+--------------------" << std::endl;

    for (int step = 0; step <= 50; ++step) {
        float t = step * 0.01f;

        // 0.3秒時点で外乱（負荷トルク 0.05 Nm）を印加
        if (step == 30) {
            load_torque = 0.05f;
        }

        float pv = motor.speed_rpm();
        float voltage = motor_pid.update(target_rpm, pv);
        motor.update(voltage, load_torque, 0.01f);

        if (step % 10 == 0 || step == 30 || step == 35) {
            std::cout << "  " << std::setw(8) << std::fixed << std::setprecision(2) << t
                      << " | " << std::setw(10) << std::setprecision(0) << target_rpm
                      << " | " << std::setw(9) << std::setprecision(1) << pv
                      << " | " << std::setw(11) << std::setprecision(2) << voltage
                      << " | " << (step >= 30 ? "Load Torque +0.05 Nm Applied" : "Normal Operation") << std::endl;
        }
    }

    // -----------------------------------------------------------------
    // Scenario 3: バプレス転送 (Bumpless Manual/Auto Transfer)
    // -----------------------------------------------------------------
    std::cout << "\n>>> Scenario 3: Bumpless Transfer Demonstration <<<" << std::endl;
    embed_pid::Controller bumpless_pid(2.0f, 0.5f, 0.0f, 0.1f);
    bumpless_pid.set_output_limits(0.0f, 100.0f);

    // 手動モードで 45% 出力を維持
    bumpless_pid.set_auto_mode(false, 30.0f);
    bumpless_pid.set_manual_output(45.0f);
    float manual_out = bumpless_pid.update(100.0f, 30.0f);
    std::cout << "  [Manual Mode] Fixed Output: " << manual_out << " % (PV: 30.0, Target: 100.0)" << std::endl;

    // 手動 -> 自動 切り替え
    bumpless_pid.set_auto_mode(true, 30.0f);
    float auto_out_1 = bumpless_pid.update(100.0f, 30.0f);
    std::cout << "  [Switched to Auto] First Step Output: " << auto_out_1 << " % (Bumplessly smooth, no bump spike!)" << std::endl;

    // -----------------------------------------------------------------
    // Scenario 4: Ziegler-Nichols チューニング計算
    // -----------------------------------------------------------------
    std::cout << "\n>>> Scenario 4: Ziegler-Nichols Auto-Tuning Calculation <<<" << std::endl;
    float ku = 8.5f; // 限界感度
    float tu = 1.8f; // 限界周期 1.8秒
    float kp_calc = 0, ki_calc = 0, kd_calc = 0;

    pid_tune_ziegler_nichols(ku, tu, ZN_TUNING_PID_CLASSIC, &kp_calc, &ki_calc, &kd_calc);
    std::cout << "  Input: Ku = " << ku << ", Tu = " << tu << " s" << std::endl;
    std::cout << "  Classic PID Calculated Parameters:" << std::endl;
    std::cout << "    -> Kp = " << kp_calc << std::endl;
    std::cout << "    -> Ki = " << ki_calc << " (Ti = " << (kp_calc / ki_calc) << " s)" << std::endl;
    std::cout << "    -> Kd = " << kd_calc << " (Td = " << (kd_calc / kp_calc) << " s)" << std::endl;

    std::cout << "\n=====================================================" << std::endl;
    std::cout << " embed_pid demonstration completed successfully!" << std::endl;
    std::cout << "=====================================================" << std::endl;
    return 0;
}
