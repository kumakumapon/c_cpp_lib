#pragma once
#ifndef EMBED_PATH_HPP
#define EMBED_PATH_HPP

#include "path_types.h"
#include "embed_path.h"
#include <vector>

namespace embed_path {

class GridPlanner {
public:
    GridPlanner(size_t width, size_t height, float resolution = 1.0f) {
        path_grid_init(&map_, width, height, resolution);
    }

    void set_obstacle(size_t x, size_t y, bool is_obstacle = true) {
        path_grid_set_obstacle(&map_, x, y, is_obstacle);
    }

    bool is_obstacle(size_t x, size_t y) const {
        return path_grid_is_obstacle(&map_, x, y);
    }

    bool plan(size_t start_gx, size_t start_gy, size_t goal_gx, size_t goal_gy, path_trajectory_t &traj) const {
        return path_plan_astar(&map_, start_gx, start_gy, goal_gx, goal_gy, &traj);
    }

    const path_grid_map_t &map() const { return map_; }

private:
    path_grid_map_t map_{};
};

class PurePursuit {
public:
    PurePursuit(float lookahead_m, float target_v_m_s, float max_w_rad_s = 2.0f, float goal_tol_m = 0.1f) {
        params_.lookahead_distance = lookahead_m;
        params_.target_velocity = target_v_m_s;
        params_.max_angular_vel = max_w_rad_s;
        params_.goal_tolerance = goal_tol_m;
    }

    path_control_cmd_t compute(const path_trajectory_t &traj, float x, float y, float theta) const {
        path_control_cmd_t cmd{};
        path_pursuit_compute(&params_, &traj, x, y, theta, &cmd);
        return cmd;
    }

private:
    path_pursuit_params_t params_{};
};

} // namespace embed_path

#endif /* EMBED_PATH_HPP */
