#include "embed_kinematics/kinematics.h"
#include <math.h>

#define EPSILON 0.00001f

/* ---------------------------------------------------------
 * 差動2輪 (Differential Drive)
 * --------------------------------------------------------- */

void diff_drive_forward(const diff_drive_params_t *params, const diff_wheel_speeds_t *wheels, twist2d_t *twist) {
    if (!params || !wheels || !twist) return;

    float r = params->wheel_radius;
    float l = params->track_width;

    float v_r = wheels->right * r;
    float v_l = wheels->left * r;

    twist->vx = 0.5f * (v_r + v_l);
    twist->vy = 0.0f;
    twist->omega = (l > EPSILON) ? ((v_r - v_l) / l) : 0.0f;
}

void diff_drive_inverse(const diff_drive_params_t *params, const twist2d_t *twist, diff_wheel_speeds_t *wheels) {
    if (!params || !twist || !wheels) return;

    float r = (params->wheel_radius > EPSILON) ? params->wheel_radius : 1.0f;
    float l = params->track_width;

    float v = twist->vx;
    float w = twist->omega;

    // 車輪の角速度 [rad/s]
    wheels->right = (v + (0.5f * w * l)) / r;
    wheels->left  = (v - (0.5f * w * l)) / r;
}

void diff_drive_odometry_update(pose2d_t *pose, float d_left, float d_right, float track_width) {
    if (!pose || track_width < EPSILON) return;

    float d_center = 0.5f * (d_right + d_left);
    float d_theta = (d_right - d_left) / track_width;

    if (fabsf(d_theta) < 0.0001f) {
        // 直進近似 (Runge-Kutta 1次)
        pose->x += d_center * cosf(pose->theta + 0.5f * d_theta);
        pose->y += d_center * sinf(pose->theta + 0.5f * d_theta);
    } else {
        // 円弧近似
        pose->x += d_center * cosf(pose->theta + 0.5f * d_theta);
        pose->y += d_center * sinf(pose->theta + 0.5f * d_theta);
    }

    pose->theta += d_theta;

    // 角度正規化 (-PI 〜 +PI)
    while (pose->theta > 3.14159265f) pose->theta -= 2.0f * 3.14159265f;
    while (pose->theta < -3.14159265f) pose->theta += 2.0f * 3.14159265f;
}

/* ---------------------------------------------------------
 * 4輪メカナム (Mecanum Drive)
 * --------------------------------------------------------- */

void mecanum_drive_forward(const mecanum_params_t *params, const mecanum_wheel_speeds_t *wheels, twist2d_t *twist) {
    if (!params || !wheels || !twist) return;

    float r = params->wheel_radius;
    float k = 0.5f * (params->track_width + params->wheel_base);

    twist->vx    = 0.25f * r * ( wheels->fl + wheels->fr + wheels->rl + wheels->rr);
    twist->vy    = 0.25f * r * (-wheels->fl + wheels->fr + wheels->rl - wheels->rr);
    twist->omega = (k > EPSILON) ? (0.25f * r / k * (-wheels->fl + wheels->fr - wheels->rl + wheels->rr)) : 0.0f;
}

void mecanum_drive_inverse(const mecanum_params_t *params, const twist2d_t *twist, mecanum_wheel_speeds_t *wheels) {
    if (!params || !twist || !wheels) return;

    float r = (params->wheel_radius > EPSILON) ? params->wheel_radius : 1.0f;
    float k = 0.5f * (params->track_width + params->wheel_base);

    float vx = twist->vx;
    float vy = twist->vy;
    float w  = twist->omega;

    wheels->fl = (vx - vy - (k * w)) / r;
    wheels->fr = (vx + vy + (k * w)) / r;
    wheels->rl = (vx + vy - (k * w)) / r;
    wheels->rr = (vx - vy + (k * w)) / r;
}

/* ---------------------------------------------------------
 * 2自由度ロボットアーム (2-DOF Planar Arm)
 * --------------------------------------------------------- */

bool arm2dof_forward(const arm2dof_params_t *params, const arm2dof_joints_t *joints, float *x, float *y) {
    if (!params || !joints || !x || !y) return false;

    float l1 = params->l1;
    float l2 = params->l2;
    float q1 = joints->q1;
    float q2 = joints->q2;

    *x = (l1 * cosf(q1)) + (l2 * cosf(q1 + q2));
    *y = (l1 * sinf(q1)) + (l2 * sinf(q1 + q2));
    return true;
}

bool arm2dof_inverse(const arm2dof_params_t *params, float x, float y, bool elbow_up, arm2dof_joints_t *joints) {
    if (!params || !joints) return false;

    float l1 = params->l1;
    float l2 = params->l2;

    float r2 = (x * x) + (y * y);
    float cos_q2 = (r2 - (l1 * l1) - (l2 * l2)) / (2.0f * l1 * l2);

    if (cos_q2 < -1.0f || cos_q2 > 1.0f) {
        return false; // 到達不能点 (Workspace外)
    }

    float sin_q2 = sqrtf(1.0f - (cos_q2 * cos_q2));
    if (!elbow_up) {
        sin_q2 = -sin_q2;
    }

    float q2 = atan2f(sin_q2, cos_q2);
    float k1 = l1 + (l2 * cos_q2);
    float k2 = l2 * sin_q2;
    float q1 = atan2f(y, x) - atan2f(k2, k1);

    joints->q1 = q1;
    joints->q2 = q2;
    return true;
}
