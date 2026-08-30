#pragma once
#ifndef EMBED_KINEMATICS_HPP
#define EMBED_KINEMATICS_HPP

#include "kinematics_types.h"
#include "kinematics.h"

namespace embed_kinematics {

class DiffDrive {
public:
    DiffDrive(float wheel_radius, float track_width) {
        params_.wheel_radius = wheel_radius;
        params_.track_width = track_width;
    }

    twist2d_t forward(float left_wheel_rad_s, float right_wheel_rad_s) const {
        diff_wheel_speeds_t ws{left_wheel_rad_s, right_wheel_rad_s};
        twist2d_t tw{};
        diff_drive_forward(&params_, &ws, &tw);
        return tw;
    }

    diff_wheel_speeds_t inverse(float vx, float omega) const {
        twist2d_t tw{vx, 0.0f, omega};
        diff_wheel_speeds_t ws{};
        diff_drive_inverse(&params_, &tw, &ws);
        return ws;
    }

    void update_odometry(pose2d_t &pose, float d_left, float d_right) const {
        diff_drive_odometry_update(&pose, d_left, d_right, params_.track_width);
    }

private:
    diff_drive_params_t params_{};
};

class MecanumDrive {
public:
    MecanumDrive(float wheel_radius, float track_width, float wheel_base) {
        params_.wheel_radius = wheel_radius;
        params_.track_width = track_width;
        params_.wheel_base = wheel_base;
    }

    twist2d_t forward(const mecanum_wheel_speeds_t &wheels) const {
        twist2d_t tw{};
        mecanum_drive_forward(&params_, &wheels, &tw);
        return tw;
    }

    mecanum_wheel_speeds_t inverse(float vx, float vy, float omega) const {
        twist2d_t tw{vx, vy, omega};
        mecanum_wheel_speeds_t ws{};
        mecanum_drive_inverse(&params_, &tw, &ws);
        return ws;
    }

private:
    mecanum_params_t params_{};
};

class Arm2DOF {
public:
    Arm2DOF(float l1, float l2) {
        params_.l1 = l1;
        params_.l2 = l2;
    }

    bool forward(float q1, float q2, float &x, float &y) const {
        arm2dof_joints_t joints{q1, q2};
        return arm2dof_forward(&params_, &joints, &x, &y);
    }

    bool inverse(float x, float y, bool elbow_up, float &q1, float &q2) const {
        arm2dof_joints_t joints{};
        if (arm2dof_inverse(&params_, x, y, elbow_up, &joints)) {
            q1 = joints.q1;
            q2 = joints.q2;
            return true;
        }
        return false;
    }

private:
    arm2dof_params_t params_{};
};

} // namespace embed_kinematics

#endif /* EMBED_KINEMATICS_HPP */
