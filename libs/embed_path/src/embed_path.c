#include "embed_path/embed_path.h"
#include <string.h>
#include <math.h>
#include <float.h>

#define INF_COST 1e9f

void path_grid_init(path_grid_map_t *map, size_t width, size_t height, float resolution) {
    if (!map) return;
    memset(map, 0, sizeof(path_grid_map_t));
    map->width = (width > PATH_GRID_MAX_WIDTH) ? PATH_GRID_MAX_WIDTH : width;
    map->height = (height > PATH_GRID_MAX_HEIGHT) ? PATH_GRID_MAX_HEIGHT : height;
    map->resolution = (resolution > 0.0f) ? resolution : 1.0f;
    map->origin_x = 0.0f;
    map->origin_y = 0.0f;
}

void path_grid_set_obstacle(path_grid_map_t *map, size_t x, size_t y, bool is_obstacle) {
    if (!map || x >= map->width || y >= map->height) return;
    map->grid[y][x] = is_obstacle ? 1 : 0;
}

bool path_grid_is_obstacle(const path_grid_map_t *map, size_t x, size_t y) {
    if (!map || x >= map->width || y >= map->height) return true;
    return (map->grid[y][x] != 0);
}

typedef struct {
    float g_score;
    float f_score;
    int parent_x;
    int parent_y;
    bool in_open;
    bool in_closed;
} astar_node_t;

static float heuristic(int x1, int y1, int x2, int y2) {
    float dx = (float)(x1 - x2);
    float dy = (float)(y1 - y2);
    return sqrtf(dx * dx + dy * dy);
}

bool path_plan_astar(const path_grid_map_t *map,
                     size_t start_gx, size_t start_gy,
                     size_t goal_gx, size_t goal_gy,
                     path_trajectory_t *traj) {
    if (!map || !traj) return false;
    if (start_gx >= map->width || start_gy >= map->height) return false;
    if (goal_gx >= map->width || goal_gy >= map->height) return false;
    if (path_grid_is_obstacle(map, start_gx, start_gy) || path_grid_is_obstacle(map, goal_gx, goal_gy)) {
        return false;
    }

    memset(traj, 0, sizeof(path_trajectory_t));

    astar_node_t nodes[PATH_GRID_MAX_HEIGHT][PATH_GRID_MAX_WIDTH];
    for (size_t y = 0; y < map->height; ++y) {
        for (size_t x = 0; x < map->width; ++x) {
            nodes[y][x].g_score = INF_COST;
            nodes[y][x].f_score = INF_COST;
            nodes[y][x].parent_x = -1;
            nodes[y][x].parent_y = -1;
            nodes[y][x].in_open = false;
            nodes[y][x].in_closed = false;
        }
    }

    nodes[start_gy][start_gx].g_score = 0.0f;
    nodes[start_gy][start_gx].f_score = heuristic((int)start_gx, (int)start_gy, (int)goal_gx, (int)goal_gy);
    nodes[start_gy][start_gx].in_open = true;

    // 8方向探索 (4近傍 + 斜め)
    const int dx[] = { 1, -1,  0,  0,  1, -1,  1, -1};
    const int dy[] = { 0,  0,  1, -1,  1,  1, -1, -1};
    const float move_cost[] = {1.0f, 1.0f, 1.0f, 1.0f, 1.4142f, 1.4142f, 1.4142f, 1.4142f};

    bool found = false;

    while (true) {
        // Open set から最小 f_score ノードを探索
        float min_f = INF_COST;
        int cur_x = -1, cur_y = -1;

        for (size_t y = 0; y < map->height; ++y) {
            for (size_t x = 0; x < map->width; ++x) {
                if (nodes[y][x].in_open && nodes[y][x].f_score < min_f) {
                    min_f = nodes[y][x].f_score;
                    cur_x = (int)x;
                    cur_y = (int)y;
                }
            }
        }

        if (cur_x == -1 || cur_y == -1) {
            break; // 経路なし
        }

        if (cur_x == (int)goal_gx && cur_y == (int)goal_gy) {
            found = true;
            break; // ゴール到達
        }

        nodes[cur_y][cur_x].in_open = false;
        nodes[cur_y][cur_x].in_closed = true;

        for (int i = 0; i < 8; ++i) {
            int nx = cur_x + dx[i];
            int ny = cur_y + dy[i];

            if (nx < 0 || nx >= (int)map->width || ny < 0 || ny >= (int)map->height) continue;
            if (map->grid[ny][nx] != 0) continue;
            if (nodes[ny][nx].in_closed) continue;

            float tentative_g = nodes[cur_y][cur_x].g_score + move_cost[i];
            if (tentative_g < nodes[ny][nx].g_score) {
                nodes[ny][nx].parent_x = cur_x;
                nodes[ny][nx].parent_y = cur_y;
                nodes[ny][nx].g_score = tentative_g;
                nodes[ny][nx].f_score = tentative_g + heuristic(nx, ny, (int)goal_gx, (int)goal_gy);
                nodes[ny][nx].in_open = true;
            }
        }
    }

    if (!found) return false;

    // ゴールから親をたどってパスを復元
    path_point_t temp_pts[PATH_MAX_WAYPOINTS];
    size_t pt_count = 0;

    int cx = (int)goal_gx;
    int cy = (int)goal_gy;

    while (cx != -1 && cy != -1) {
        /* 経路がウェイポイント数の上限を超えるときは、切り捨てた不完全な経路を返さず失敗にする */
        if (pt_count >= PATH_MAX_WAYPOINTS) return false;
        temp_pts[pt_count].x = map->origin_x + ((float)cx + 0.5f) * map->resolution;
        temp_pts[pt_count].y = map->origin_y + ((float)cy + 0.5f) * map->resolution;
        pt_count++;

        int px = nodes[cy][cx].parent_x;
        int py = nodes[cy][cx].parent_y;
        cx = px;
        cy = py;
    }

    // 順序をスタート -> ゴールに反転
    traj->count = pt_count;
    float len = 0.0f;
    for (size_t i = 0; i < pt_count; ++i) {
        traj->points[i] = temp_pts[pt_count - 1 - i];
        if (i > 0) {
            float ddx = traj->points[i].x - traj->points[i - 1].x;
            float ddy = traj->points[i].y - traj->points[i - 1].y;
            len += sqrtf(ddx * ddx + ddy * ddy);
        }
    }
    traj->total_length = len;

    return true;
}

void path_pursuit_compute(const path_pursuit_params_t *params,
                          const path_trajectory_t *traj,
                          float current_x, float current_y, float current_theta,
                          path_control_cmd_t *cmd) {
    if (!params || !traj || !cmd || traj->count == 0) {
        if (cmd) {
            cmd->linear_velocity = 0.0f;
            cmd->angular_velocity = 0.0f;
            cmd->is_goal_reached = true;
        }
        return;
    }

    const path_point_t *goal = &traj->points[traj->count - 1];
    float dist_to_goal = sqrtf((goal->x - current_x) * (goal->x - current_x) + (goal->y - current_y) * (goal->y - current_y));

    if (dist_to_goal <= params->goal_tolerance) {
        cmd->linear_velocity = 0.0f;
        cmd->angular_velocity = 0.0f;
        cmd->is_goal_reached = true;
        return;
    }

    cmd->is_goal_reached = false;

    // 前方注視点 (Look-ahead Point) を探索
    float ld = params->lookahead_distance;
    if (ld < 0.01f) ld = 0.01f;

    /* 通り過ぎた点を目標にしないよう、機体に最も近い点から先を探す */
    size_t nearest_idx = 0;
    float nearest_d2 = FLT_MAX;
    for (size_t i = 0; i < traj->count; ++i) {
        float ex = traj->points[i].x - current_x;
        float ey = traj->points[i].y - current_y;
        float d2 = ex * ex + ey * ey;
        if (d2 < nearest_d2) {
            nearest_d2 = d2;
            nearest_idx = i;
        }
    }

    size_t target_idx = traj->count - 1;
    for (size_t i = nearest_idx; i < traj->count; ++i) {
        float d = sqrtf((traj->points[i].x - current_x) * (traj->points[i].x - current_x) +
                        (traj->points[i].y - current_y) * (traj->points[i].y - current_y));
        if (d >= ld) {
            target_idx = i;
            break;
        }
    }

    path_point_t target_pt = traj->points[target_idx];

    // 車体座標系への変換
    float dx = target_pt.x - current_x;
    float dy = target_pt.y - current_y;

    float angle_to_target = atan2f(dy, dx);
    float alpha = angle_to_target - current_theta;

    // 角度正規化 (-PI 〜 +PI)
    while (alpha > 3.14159265f) alpha -= 2.0f * 3.14159265f;
    while (alpha < -3.14159265f) alpha += 2.0f * 3.14159265f;

    float actual_dist = sqrtf(dx * dx + dy * dy);
    if (actual_dist < 0.01f) actual_dist = 0.01f;

    // 曲率 kappa = 2 * sin(alpha) / Ld
    float kappa = (2.0f * sinf(alpha)) / actual_dist;
    float v = params->target_velocity;
    float w = kappa * v;

    if (params->max_angular_vel > 0.0f) {
        if (w > params->max_angular_vel) w = params->max_angular_vel;
        if (w < -params->max_angular_vel) w = -params->max_angular_vel;
    }

    cmd->linear_velocity = v;
    cmd->angular_velocity = w;
}
