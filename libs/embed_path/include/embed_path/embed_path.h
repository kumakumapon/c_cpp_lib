#pragma once
#ifndef EMBED_PATH_H
#define EMBED_PATH_H

#include "path_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------
 * グリッドマップ & A* 経路探索
 * --------------------------------------------------------- */

void path_grid_init(path_grid_map_t *map, size_t width, size_t height, float resolution);
void path_grid_set_obstacle(path_grid_map_t *map, size_t x, size_t y, bool is_obstacle);
bool path_grid_is_obstacle(const path_grid_map_t *map, size_t x, size_t y);

bool path_plan_astar(const path_grid_map_t *map,
                     size_t start_gx, size_t start_gy,
                     size_t goal_gx, size_t goal_gy,
                     path_trajectory_t *traj);

/* ---------------------------------------------------------
 * Pure Pursuit 経路追従
 * --------------------------------------------------------- */

void path_pursuit_compute(const path_pursuit_params_t *params,
                          const path_trajectory_t *traj,
                          float current_x, float current_y, float current_theta,
                          path_control_cmd_t *cmd);

#ifdef __cplusplus
}
#endif

#endif /* EMBED_PATH_H */
