#pragma once
#include "pid_types.h"
#include "pid_controller.h"

namespace embed_pid {

class Controller {
public:
    Controller(float kp = 1.0f, float ki = 0.0f, float kd = 0.0f, float dt = 0.01f) {
        pid_config_t cfg;
        pid_get_default_config(&cfg);
        cfg.kp = kp;
        cfg.ki = ki;
        cfg.kd = kd;
        cfg.dt = dt;
        pid_init(&pid_, &cfg);
    }

    explicit Controller(const pid_config_t &config) {
        pid_init(&pid_, &config);
    }

    Controller &set_gains(float kp, float ki, float kd) {
        pid_.config.kp = kp;
        pid_.config.ki = ki;
        pid_.config.kd = kd;
        return *this;
    }

    Controller &set_dt(float dt) {
        pid_.config.dt = dt;
        return *this;
    }

    Controller &set_output_limits(float min_val, float max_val) {
        pid_.config.out_min = min_val;
        pid_.config.out_max = max_val;
        return *this;
    }

    Controller &set_deadband(float deadband) {
        pid_.config.deadband = deadband;
        return *this;
    }

    Controller &set_filter_time_constant(float tau_d) {
        pid_.config.tau_d = tau_d;
        return *this;
    }

    Controller &set_mode(pid_algorithm_mode_t mode) {
        pid_.config.mode = mode;
        return *this;
    }

    Controller &set_anti_windup(pid_anti_windup_t aw, float kt = 0.0f) {
        pid_.config.anti_windup = aw;
        pid_.config.kt = kt;
        return *this;
    }

    void reset() {
        pid_reset(&pid_);
    }

    float update(float setpoint, float process_variable) {
        return pid_compute(&pid_, setpoint, process_variable);
    }

    void set_auto_mode(bool is_auto, float current_pv = 0.0f) {
        pid_set_mode(&pid_, is_auto, current_pv);
    }

    void set_manual_output(float manual_val) {
        pid_set_manual_output(&pid_, manual_val);
    }

    bool is_auto() const { return pid_.is_auto; }

    /* 各項の取得 (監視用) */
    float p_term() const { return pid_.p_term; }
    float i_term() const { return pid_.i_term; }
    float d_term() const { return pid_.d_term; }
    float last_output() const { return pid_.saturated_output; }

    const pid_config_t &config() const { return pid_.config; }
    embed_pid_t *raw_handle() { return &pid_; }
    const embed_pid_t *raw_handle() const { return &pid_; }

private:
    embed_pid_t pid_{};
};

} // namespace embed_pid
