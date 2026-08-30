#pragma once
#ifndef PATH_TYPES_H
#define PATH_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PATH_MAX_WAYPOINTS         64
#define PATH_GRID_MAX_WIDTH        32
#define PATH_GRID_MAX_HEIGHT       32

/**
 * @brief 2D ウェイポイント座標
 */
typedef struct {
    float x;
    float y;
} path_point_t;

/**
 * @brief 経路計画結果
 */
typedef struct {
    path_point_t points[PATH_MAX_WAYPOINTS];
    size_t count;
    float total_length;
} path_trajectory_t;

/**
 * @brief 2D グリッドマップ構造体
 */
typedef struct {
    uint8_t grid[PATH_GRID_MAX_HEIGHT][PATH_GRID_MAX_WIDTH]; // 0: 通行可, 1: 障害物
    size_t width;
    size_t height;
    float resolution; // 1セルあたりの実空間サイズ [m]
    float origin_x;
    float origin_y;
} path_grid_map_t;

/**
 * @brief Pure Pursuit 追従パラメータ
 */
typedef struct {
    float lookahead_distance; /**< 前方注視距離 Ld [m] */
    float target_velocity;    /**< 目標並進速度 [m/s] */
    float max_angular_vel;    /**< 最大旋回角速度 [rad/s] */
    float goal_tolerance;     /**< ゴール到達判定半径 [m] */
} path_pursuit_params_t;

/**
 * @brief 追従制御コマンド出力
 */
typedef struct {
    float linear_velocity;  /**< 指示並進速度 [m/s] */
    float angular_velocity; /**< 指示旋回角速度 [rad/s] */
    bool is_goal_reached;   /**< ゴール到達フラグ */
} path_control_cmd_t;

#ifdef __cplusplus
}
#endif

#endif /* PATH_TYPES_H */
