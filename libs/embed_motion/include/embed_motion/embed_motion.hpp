#pragma once
#include "motion_types.h"
#include "motion_profile.h"
#include <vector>
#include <array>

namespace embed_motion {

class SingleAxis {
public:
    SingleAxis(float max_v, float max_a, float max_d, float max_j = 0.0f) {
        constraints_.max_velocity = max_v;
        constraints_.max_acceleration = max_a;
        constraints_.max_deceleration = max_d;
        constraints_.max_jerk = max_j;
    }

    bool plan(float start_pos, float target_pos,
              motion_profile_type_t type = MOTION_PROFILE_TRAPEZOIDAL,
              float start_vel = 0.0f) {
        return motion_profile_plan(&profile_, type, &constraints_, start_pos, target_pos, start_vel);
    }

    const motion_state_t &step(float dt) {
        return *motion_profile_step(&profile_, dt);
    }

    motion_state_t evaluate(float t) const {
        motion_state_t st{};
        motion_profile_evaluate(&profile_, t, &st);
        return st;
    }

    bool is_finished() const { return profile_.state.is_finished; }
    float total_time() const { return profile_.total_time; }
    const motion_state_t &state() const { return profile_.state; }
    const motion_profile_t &raw() const { return profile_; }

private:
    motion_constraints_t constraints_{};
    motion_profile_t profile_{};
};

template <size_t N>
class MultiAxis {
    static_assert(N <= MOTION_MAX_AXES, "Exceeded MOTION_MAX_AXES");
public:
    MultiAxis(float max_v, float max_a, float max_d) {
        constraints_.max_velocity = max_v;
        constraints_.max_acceleration = max_a;
        constraints_.max_deceleration = max_d;
        constraints_.max_jerk = 0.0f;
    }

    bool plan(const std::array<float, N> &start_pos, const std::array<float, N> &target_pos) {
        return motion_multi_axis_plan(&multi_, N, &constraints_, start_pos.data(), target_pos.data());
    }

    bool step(float dt, std::array<float, N> &current_pos, std::array<float, N> &current_vel) {
        return motion_multi_axis_step(&multi_, dt, current_pos.data(), current_vel.data());
    }

    float total_time() const { return multi_.lead_profile.total_time; }

private:
    motion_constraints_t constraints_{};
    motion_multi_axis_t multi_{};
};

} // namespace embed_motion
