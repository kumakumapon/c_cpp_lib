#pragma once
#ifndef EMBED_KINEMATICS_H
#define EMBED_KINEMATICS_H

#include "kinematics_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------
 * 差動2輪 (Differential Drive)
 * --------------------------------------------------------- */

void diff_drive_forward(const diff_drive_params_t *params, const diff_wheel_speeds_t *wheels, twist2d_t *twist);
void diff_drive_inverse(const diff_drive_params_t *params, const twist2d_t *twist, diff_wheel_speeds_t *wheels);
void diff_drive_odometry_update(pose2d_t *pose, float d_left, float d_right, float track_width);

/* ---------------------------------------------------------
 * 4輪メカナム (Mecanum Drive)
 * --------------------------------------------------------- */

void mecanum_drive_forward(const mecanum_params_t *params, const mecanum_wheel_speeds_t *wheels, twist2d_t *twist);
void mecanum_drive_inverse(const mecanum_params_t *params, const twist2d_t *twist, mecanum_wheel_speeds_t *wheels);

/* ---------------------------------------------------------
 * 2自由度ロボットアーム (2-DOF Planar Arm)
 * --------------------------------------------------------- */

bool arm2dof_forward(const arm2dof_params_t *params, const arm2dof_joints_t *joints, float *x, float *y);
bool arm2dof_inverse(const arm2dof_params_t *params, float x, float y, bool elbow_up, arm2dof_joints_t *joints);

#ifdef __cplusplus
}
#endif

#endif /* EMBED_KINEMATICS_H */
