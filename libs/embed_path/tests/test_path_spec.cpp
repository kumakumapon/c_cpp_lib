// A* の経路の妥当性(障害物回避・最短性・到達不能・境界)と、純追跡の数式を固定するテスト。
#include "embed_test/embed_test.hpp"
#include <cmath>
#include <cstdint>
#include <queue>
#include <vector>

#include "embed_path/embed_path.h"

namespace {
constexpr double kTol = 1e-4;
constexpr double kPi = 3.14159265358979323846;
constexpr double kDiag = 1.4142; // 実装の斜め移動コスト

struct Cell {
    int x, y;
};

Cell cell_of(const path_grid_map_t &m, const path_point_t &p) {
    return {int(std::floor((p.x - m.origin_x) / m.resolution)), int(std::floor((p.y - m.origin_y) / m.resolution))};
}

path_grid_map_t make_map(size_t w, size_t h, float res = 1.0f) {
    path_grid_map_t m;
    path_grid_init(&m, w, h, res);
    return m;
}

// 独立した参照実装: 8 近傍・同じ移動コストの Dijkstra。到達不能なら負を返す
double reference_cost(const path_grid_map_t &m, int sx, int sy, int gx, int gy) {
    const int W = int(m.width), H = int(m.height);
    std::vector<double> dist(size_t(W * H), 1e18);
    using Item = std::pair<double, int>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> pq;
    dist[size_t(sy * W + sx)] = 0;
    pq.push({0, sy * W + sx});
    while (!pq.empty()) {
        auto [d, id] = pq.top();
        pq.pop();
        if (d > dist[size_t(id)]) continue;
        const int cx = id % W, cy = id / W;
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                if (!dx && !dy) continue;
                const int nx = cx + dx, ny = cy + dy;
                if (nx < 0 || ny < 0 || nx >= W || ny >= H || m.grid[ny][nx]) continue;
                const double nd = d + ((dx && dy) ? kDiag : 1.0);
                if (nd < dist[size_t(ny * W + nx)]) {
                    dist[size_t(ny * W + nx)] = nd;
                    pq.push({nd, ny * W + nx});
                }
            }
    }
    const double r = dist[size_t(gy * W + gx)];
    return r > 1e17 ? -1.0 : r;
}

// 経路が正しいこと: 始点から終点までで、全点が空きセル、隣り合う点が隣接セル(斜め含む)。移動コストの合計を返す
double check_path(const path_grid_map_t &m, const path_trajectory_t &t, int sx, int sy, int gx, int gy) {
    if (!CHECK(t.count >= 1)) return -1.0;
    const Cell first = cell_of(m, t.points[0]), last = cell_of(m, t.points[t.count - 1]);
    CHECK(first.x == sx && first.y == sy);
    CHECK(last.x == gx && last.y == gy);
    double cost = 0, geometric = 0;
    for (size_t i = 0; i < t.count; ++i) {
        const Cell c = cell_of(m, t.points[i]);
        CHECK(!path_grid_is_obstacle(&m, size_t(c.x), size_t(c.y))); // 障害物セルを通らない
        // セル中心を通る
        CHECK_NEAR(t.points[i].x, m.origin_x + (c.x + 0.5) * m.resolution, 1e-5);
        CHECK_NEAR(t.points[i].y, m.origin_y + (c.y + 0.5) * m.resolution, 1e-5);
        if (i) {
            const Cell p = cell_of(m, t.points[i - 1]);
            const int dx = std::abs(c.x - p.x), dy = std::abs(c.y - p.y);
            CHECK(dx <= 1 && dy <= 1 && (dx || dy)); // 同じ点を繰り返さず、隣のセルへ
            cost += (dx && dy) ? kDiag : 1.0;
            geometric += std::hypot(t.points[i].x - t.points[i - 1].x, t.points[i].y - t.points[i - 1].y);
        }
    }
    CHECK_NEAR(t.total_length, geometric, 1e-4); // total_length は各区間の長さの和
    return cost;
}

uint32_t g_seed = 12345;
uint32_t next_rand() { return g_seed = g_seed * 1664525u + 1013904223u; }
} // namespace

// ---- グリッド ----------------------------------------------------------------

TEST_CASE(grid_init_clamps_and_defaults) {
    path_grid_map_t m;
    m.grid[0][0] = 1;
    path_grid_init(&m, 100, 200, 0.25f);
    CHECK_EQ(m.width, size_t(PATH_GRID_MAX_WIDTH));
    CHECK_EQ(m.height, size_t(PATH_GRID_MAX_HEIGHT));
    CHECK_NEAR(m.resolution, 0.25, 1e-7);
    CHECK_EQ(int(m.grid[0][0]), 0); // 全セル空き
    CHECK_NEAR(m.origin_x, 0.0, 1e-7);
    path_grid_init(&m, 7, 5, 0.0f); // 解像度が 0 以下なら 1.0
    CHECK_EQ(m.width, size_t(7));
    CHECK_EQ(m.height, size_t(5));
    CHECK_NEAR(m.resolution, 1.0, 1e-7);
    path_grid_init(&m, 7, 5, -2.0f);
    CHECK_NEAR(m.resolution, 1.0, 1e-7);
    path_grid_init(nullptr, 1, 1, 1.0f); // 落ちない
}

TEST_CASE(grid_obstacle_set_get_and_bounds) {
    path_grid_map_t m = make_map(4, 3);
    CHECK(!path_grid_is_obstacle(&m, 0, 0));
    path_grid_set_obstacle(&m, 3, 2, true);
    CHECK(path_grid_is_obstacle(&m, 3, 2));
    CHECK(!path_grid_is_obstacle(&m, 2, 3 - 1));
    path_grid_set_obstacle(&m, 3, 2, false);
    CHECK(!path_grid_is_obstacle(&m, 3, 2));
    // 範囲外は「障害物」として扱い、設定は無視する (幅 4 のとき x=4 が範囲外)
    CHECK(path_grid_is_obstacle(&m, 4, 0));
    CHECK(path_grid_is_obstacle(&m, 0, 3));
    CHECK(!path_grid_is_obstacle(&m, 3, 0)); // 境界の内側
    CHECK(!path_grid_is_obstacle(&m, 0, 2));
    path_grid_set_obstacle(&m, 4, 0, true);
    path_grid_set_obstacle(&m, 0, 3, true);
    CHECK(m.grid[0][4] == 0 && m.grid[3][0] == 0);
    CHECK(path_grid_is_obstacle(nullptr, 0, 0));
    path_grid_set_obstacle(nullptr, 0, 0, true);
}

// ---- A* ----------------------------------------------------------------------

TEST_CASE(astar_open_field_known_lengths) {
    path_grid_map_t m = make_map(10, 10);
    path_trajectory_t t;
    // 直線: (0,0) → (5,0) は 6 点・長さ 5
    REQUIRE(path_plan_astar(&m, 0, 0, 5, 0, &t));
    CHECK_EQ(t.count, size_t(6));
    CHECK_NEAR(t.total_length, 5.0, kTol);
    CHECK_NEAR(check_path(m, t, 0, 0, 5, 0), 5.0, 1e-3);
    // 斜め: (0,0) → (4,4) は 5 点・長さ 4√2
    REQUIRE(path_plan_astar(&m, 0, 0, 4, 4, &t));
    CHECK_EQ(t.count, size_t(5));
    CHECK_NEAR(t.total_length, 4 * std::sqrt(2.0), kTol);
    // 斜め 2 + 直進 3: (0,0) → (5,2) の長さは 3 + 2√2
    REQUIRE(path_plan_astar(&m, 0, 0, 5, 2, &t));
    CHECK_EQ(t.count, size_t(6));
    CHECK_NEAR(t.total_length, 3 + 2 * std::sqrt(2.0), kTol);
    // 逆向きも同じ長さ
    REQUIRE(path_plan_astar(&m, 5, 2, 0, 0, &t));
    CHECK_NEAR(t.total_length, 3 + 2 * std::sqrt(2.0), kTol);
    check_path(m, t, 5, 2, 0, 0);
}

TEST_CASE(astar_start_equals_goal) {
    path_grid_map_t m = make_map(5, 5, 2.0f);
    m.origin_x = 10.0f;
    m.origin_y = -4.0f;
    path_trajectory_t t;
    REQUIRE(path_plan_astar(&m, 2, 3, 2, 3, &t));
    CHECK_EQ(t.count, size_t(1));
    CHECK_NEAR(t.total_length, 0.0, kTol);
    // セル中心 = origin + (index + 0.5) * resolution
    CHECK_NEAR(t.points[0].x, 10.0 + 2.5 * 2.0, kTol);
    CHECK_NEAR(t.points[0].y, -4.0 + 3.5 * 2.0, kTol);
}

TEST_CASE(astar_scales_with_resolution_and_origin) {
    path_grid_map_t m = make_map(8, 8, 0.5f);
    m.origin_x = 1.0f;
    m.origin_y = 2.0f;
    path_trajectory_t t;
    REQUIRE(path_plan_astar(&m, 1, 1, 6, 1, &t));
    CHECK_EQ(t.count, size_t(6));
    CHECK_NEAR(t.points[0].x, 1.0 + 1.5 * 0.5, kTol);
    CHECK_NEAR(t.points[0].y, 2.0 + 1.5 * 0.5, kTol);
    CHECK_NEAR(t.points[5].x, 1.0 + 6.5 * 0.5, kTol);
    CHECK_NEAR(t.total_length, 5 * 0.5, kTol); // 長さは実空間 [m]
}

TEST_CASE(astar_routes_around_walls_and_through_gaps) {
    path_grid_map_t m = make_map(10, 10);
    for (size_t y = 0; y < 10; ++y)
        if (y != 3) path_grid_set_obstacle(&m, 5, y, true); // x=5 の壁、y=3 に隙間
    path_trajectory_t t;
    REQUIRE(path_plan_astar(&m, 0, 3, 9, 3, &t));
    CHECK_NEAR(t.total_length, 9.0, kTol); // 隙間は一直線上
    CHECK_NEAR(check_path(m, t, 0, 3, 9, 3), 9.0, 1e-3);

    REQUIRE(path_plan_astar(&m, 0, 0, 9, 0, &t));
    const double cost = check_path(m, t, 0, 0, 9, 0);
    CHECK_NEAR(cost, reference_cost(m, 0, 0, 9, 0), 1e-3); // 最短
    bool through_gap = false;
    for (size_t i = 0; i < t.count; ++i)
        if (cell_of(m, t.points[i]).x == 5) through_gap = (cell_of(m, t.points[i]).y == 3);
    CHECK(through_gap); // 壁を越えるのは隙間(5,3)だけ
}

TEST_CASE(astar_reports_unreachable_and_invalid_endpoints) {
    path_grid_map_t m = make_map(10, 10);
    path_trajectory_t t;
    // 完全な壁で分断 → 到達不能
    for (size_t y = 0; y < 10; ++y)
        path_grid_set_obstacle(&m, 5, y, true);
    CHECK(!path_plan_astar(&m, 0, 0, 9, 9, &t));
    // 壁に 1 マスの隙間を開けると到達できる
    path_grid_set_obstacle(&m, 5, 9, false);
    CHECK(path_plan_astar(&m, 0, 0, 9, 9, &t));
    // ゴールが完全に囲まれている
    path_grid_map_t c = make_map(10, 10);
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
            if (dx || dy) path_grid_set_obstacle(&c, size_t(5 + dx), size_t(5 + dy), true);
    CHECK(!path_plan_astar(&c, 0, 0, 5, 5, &t));
    CHECK(!path_plan_astar(&c, 5, 5, 0, 0, &t)); // 始点が囲まれていても同じ

    // 始点 / 終点が障害物、範囲外、null
    path_grid_set_obstacle(&c, 2, 2, true);
    CHECK(!path_plan_astar(&c, 2, 2, 8, 8, &t));
    CHECK(!path_plan_astar(&c, 8, 8, 2, 2, &t));
    CHECK(!path_plan_astar(&c, 10, 0, 8, 8, &t));  // 始点 x が幅と同じ
    CHECK(!path_plan_astar(&c, 0, 10, 8, 8, &t));  // 始点 y が高さと同じ
    CHECK(!path_plan_astar(&c, 0, 0, 10, 8, &t));
    CHECK(!path_plan_astar(&c, 0, 0, 8, 10, &t));
    CHECK(path_plan_astar(&c, 0, 0, 9, 9, &t)); // 最後のセル (9,9) は範囲内
    CHECK(path_plan_astar(&c, 9, 9, 0, 0, &t));
    CHECK(!path_plan_astar(nullptr, 0, 0, 8, 8, &t));
    CHECK(!path_plan_astar(&c, 0, 0, 8, 8, nullptr));
}

TEST_CASE(astar_is_optimal_and_obstacle_free_on_random_maps) {
    // 参照実装 (Dijkstra) と、到達可否・最短コストが一致する
    int reachable = 0, unreachable = 0;
    for (int trial = 0; trial < 150; ++trial) {
        const size_t w = 6 + next_rand() % 12, h = 6 + next_rand() % 12;
        path_grid_map_t m = make_map(w, h, 0.5f);
        m.origin_x = float(int(next_rand() % 7) - 3);
        m.origin_y = float(int(next_rand() % 7) - 3);
        for (size_t y = 0; y < h; ++y)
            for (size_t x = 0; x < w; ++x)
                path_grid_set_obstacle(&m, x, y, next_rand() % 100 < 48);
        const int sx = int(next_rand() % w), sy = int(next_rand() % h), gx = int(next_rand() % w),
                  gy = int(next_rand() % h);
        path_grid_set_obstacle(&m, size_t(sx), size_t(sy), false);
        path_grid_set_obstacle(&m, size_t(gx), size_t(gy), false);

        path_trajectory_t t;
        const double want = reference_cost(m, sx, sy, gx, gy);
        const bool ok = path_plan_astar(&m, size_t(sx), size_t(sy), size_t(gx), size_t(gy), &t);
        CHECK_EQ(ok, want >= 0);
        if (ok) {
            ++reachable;
            CHECK_NEAR(check_path(m, t, sx, sy, gx, gy), want, 1e-3);
        } else {
            ++unreachable;
        }
    }
    CHECK(reachable > 30 && unreachable > 10); // 両方のケースを十分に含む
}

TEST_CASE(astar_is_optimal_on_larger_sparse_maps) {
    // 障害物が少ない広い地図では、ヒューリスティックの誤りが最短性の崩れとして現れる
    for (int trial = 0; trial < 80; ++trial) {
        const size_t w = 20 + next_rand() % 13, h = 20 + next_rand() % 13;
        path_grid_map_t m = make_map(w, h);
        for (size_t y = 0; y < h; ++y)
            for (size_t x = 0; x < w; ++x)
                path_grid_set_obstacle(&m, x, y, next_rand() % 100 < 12);
        const int sx = int(next_rand() % w), sy = int(next_rand() % h), gx = int(next_rand() % w),
                  gy = int(next_rand() % h);
        path_grid_set_obstacle(&m, size_t(sx), size_t(sy), false);
        path_grid_set_obstacle(&m, size_t(gx), size_t(gy), false);
        path_trajectory_t t;
        const double want = reference_cost(m, sx, sy, gx, gy);
        const bool ok = path_plan_astar(&m, size_t(sx), size_t(sy), size_t(gx), size_t(gy), &t);
        CHECK_EQ(ok, want >= 0);
        if (ok && want >= 0) CHECK_NEAR(check_path(m, t, sx, sy, gx, gy), want, 1e-3);
    }
}

TEST_CASE(astar_is_optimal_between_opposite_corners_of_cluttered_maps) {
    // 対角の端どうしを結ぶ長い経路では、ヒューリスティックの誤りが最短性の崩れとして現れやすい
    int reachable = 0;
    for (int trial = 0; trial < 150; ++trial) {
        const size_t w = 16 + next_rand() % 17, h = 16 + next_rand() % 17;
        path_grid_map_t m = make_map(w, h);
        const unsigned density = 20 + next_rand() % 20;
        for (size_t y = 0; y < h; ++y)
            for (size_t x = 0; x < w; ++x)
                path_grid_set_obstacle(&m, x, y, next_rand() % 100 < density);
        const bool flip = next_rand() & 1;
        const int sx = flip ? int(w) - 1 : 0, sy = 0, gx = flip ? 0 : int(w) - 1, gy = int(h) - 1;
        path_grid_set_obstacle(&m, size_t(sx), size_t(sy), false);
        path_grid_set_obstacle(&m, size_t(gx), size_t(gy), false);
        path_trajectory_t t;
        const double want = reference_cost(m, sx, sy, gx, gy);
        const bool ok = path_plan_astar(&m, size_t(sx), size_t(sy), size_t(gx), size_t(gy), &t);
        CHECK_EQ(ok, want >= 0);
        if (ok && want >= 0) {
            ++reachable;
            CHECK_NEAR(check_path(m, t, sx, sy, gx, gy), want, 1e-3);
        }
    }
    CHECK(reachable > 40);
}

TEST_CASE(astar_does_not_leave_the_map) {
    // 幅 5 x 高さ 4 の地図を全幅の壁で分断する。地図の外側(x=5 の列や y=4 の行)を回り込めてはならない
    path_grid_map_t m = make_map(5, 4);
    for (size_t x = 0; x < 5; ++x)
        path_grid_set_obstacle(&m, x, 2, true);
    path_trajectory_t t;
    CHECK(!path_plan_astar(&m, 0, 0, 0, 3, &t));
    CHECK(!path_plan_astar(&m, 4, 3, 4, 0, &t));
    path_grid_map_t n = make_map(5, 4);
    for (size_t y = 0; y < 4; ++y)
        path_grid_set_obstacle(&n, 2, y, true);
    CHECK(!path_plan_astar(&n, 0, 0, 4, 0, &t));
    CHECK(!path_plan_astar(&n, 0, 3, 4, 3, &t));
}

TEST_CASE(astar_never_returns_a_truncated_path) {
    // 蛇行する迷路は 64 点(PATH_MAX_WAYPOINTS)を超える。途中から先を切り捨てた経路を成功として返してはならない
    path_grid_map_t m = make_map(32, 32);
    for (size_t y = 1; y < 32; y += 2)
        for (size_t x = 0; x < 32; ++x)
            if (x != ((y / 2) % 2 ? 0u : 31u)) path_grid_set_obstacle(&m, x, y, true);
    path_trajectory_t t;
    const double cost = reference_cost(m, 0, 0, 0, 30);
    REQUIRE(cost > 64); // 前提: 最短経路でも 64 区間を超える
    if (path_plan_astar(&m, 0, 0, 0, 30, &t)) {
        check_path(m, t, 0, 0, 0, 30); // 成功するなら始点から終点までの完全な経路
    }
    CHECK(!path_plan_astar(&m, 0, 0, 0, 30, &t));
}

// ---- 純追跡 ------------------------------------------------------------------

namespace {
path_trajectory_t line(std::initializer_list<path_point_t> pts) {
    path_trajectory_t t{};
    for (const auto &p : pts)
        t.points[t.count++] = p;
    return t;
}

path_control_cmd_t pursue(const path_pursuit_params_t &p, const path_trajectory_t &t, float x, float y,
                          float th) {
    path_control_cmd_t c{};
    path_pursuit_compute(&p, &t, x, y, th, &c);
    return c;
}
} // namespace

TEST_CASE(pursuit_curvature_matches_two_sin_alpha_over_distance) {
    const path_pursuit_params_t p{0.9f, 0.5f, 10.0f, 0.1f};
    // 注視点 (0.6, 0.8): 距離 1、α = atan2(0.8, 0.6) → κ = 2 * 0.8 / 1 = 1.6、ω = κ v = 0.8
    path_control_cmd_t c = pursue(p, line({{0, 0}, {0.6f, 0.8f}}), 0, 0, 0);
    CHECK(!c.is_goal_reached);
    CHECK_NEAR(c.linear_velocity, 0.5, kTol);
    CHECK_NEAR(c.angular_velocity, 0.8, kTol);
    // 右側は符号が反転する
    c = pursue(p, line({{0, 0}, {0.6f, -0.8f}}), 0, 0, 0);
    CHECK_NEAR(c.angular_velocity, -0.8, kTol);
    // 正面なら直進
    c = pursue(p, line({{0, 0}, {2.0f, 0}}), 0, 0, 0);
    CHECK_NEAR(c.angular_velocity, 0.0, kTol);
    // 後方左の注視点 (-0.6, 0.8): sin α は同じなので κ も同じ
    c = pursue(p, line({{0, 0}, {-0.6f, 0.8f}}), 0, 0, 0);
    CHECK_NEAR(c.angular_velocity, 0.8, kTol);
    // 真後ろ: sin(π) = 0 で旋回しない
    c = pursue(p, line({{0, 0}, {-2.0f, 0}}), 0, 0, 0);
    CHECK_NEAR(c.angular_velocity, 0.0, 1e-3);
    // 距離が 2 になると曲率は半分: 注視点 (1.2, 1.6) → κ = 2 * 0.8 / 2 = 0.8、ω = 0.4
    c = pursue(p, line({{0, 0}, {1.2f, 1.6f}}), 0, 0, 0);
    CHECK_NEAR(c.angular_velocity, 0.4, kTol);
}

TEST_CASE(pursuit_uses_the_vehicle_pose_and_scales_with_velocity) {
    path_pursuit_params_t p{0.9f, 1.0f, 10.0f, 0.1f};
    // 機体 (1,1) が +y を向く(θ=π/2)。注視点 (1,2): 真正面 → 直進
    path_control_cmd_t c = pursue(p, line({{1, 1}, {1.0f, 2.0f}}), 1, 1, 1.5707964f);
    CHECK_NEAR(c.angular_velocity, 0.0, 1e-3);
    // 注視点 (0,1): 左(−x)は、+y を向く機体にとって左 → 正の旋回。α = π/2、κ = 2
    c = pursue(p, line({{1, 1}, {0.0f, 1.0f}}), 1, 1, 1.5707964f);
    CHECK_NEAR(c.angular_velocity, 2.0, kTol);
    // ω は v に比例
    p.target_velocity = 0.25f;
    c = pursue(p, line({{1, 1}, {0.0f, 1.0f}}), 1, 1, 1.5707964f);
    CHECK_NEAR(c.linear_velocity, 0.25, kTol);
    CHECK_NEAR(c.angular_velocity, 0.5, kTol);
    // 角度の折り返し: θ = 2π + π/2 でも同じ
    c = pursue(p, line({{1, 1}, {0.0f, 1.0f}}), 1, 1, 1.5707964f + 6.2831855f);
    CHECK_NEAR(c.angular_velocity, 0.5, 1e-3);
}

TEST_CASE(pursuit_clips_the_angular_velocity) {
    path_pursuit_params_t p{0.9f, 1.0f, 0.6f, 0.1f};
    const path_trajectory_t left = line({{0, 0}, {0.0f, 1.0f}}), right = line({{0, 0}, {0.0f, -1.0f}});
    // κ = 2、ω = 2 → 上限 0.6 に制限(左右対称)
    CHECK_NEAR(pursue(p, left, 0, 0, 0).angular_velocity, 0.6, kTol);
    CHECK_NEAR(pursue(p, right, 0, 0, 0).angular_velocity, -0.6, kTol);
    p.max_angular_vel = 2.0f; // ちょうど上限なら素通し
    CHECK_NEAR(pursue(p, left, 0, 0, 0).angular_velocity, 2.0, kTol);
    p.max_angular_vel = 3.0f;
    CHECK_NEAR(pursue(p, left, 0, 0, 0).angular_velocity, 2.0, kTol);
    p.max_angular_vel = 0.0f; // 0 以下は無制限
    CHECK_NEAR(pursue(p, left, 0, 0, 0).angular_velocity, 2.0, kTol);
}

TEST_CASE(pursuit_picks_the_first_point_at_least_the_lookahead_away) {
    const path_pursuit_params_t p{1.5f, 1.0f, 10.0f, 0.1f};
    // (1,0) は 1.0 < 1.5 なので手前。(1.5,1.5) が最初の 1.5 以上 → α = 45°、距離 1.5√2
    const path_trajectory_t t = line({{0, 0}, {1, 0}, {1.5f, 1.5f}, {10, 0}});
    const path_control_cmd_t c = pursue(p, t, 0, 0, 0);
    CHECK_NEAR(c.angular_velocity, 2 * std::sin(kPi / 4) / (1.5 * std::sqrt(2.0)), kTol);
    // ちょうど Ld の距離にある点も採用される (d >= Ld)
    const path_trajectory_t u = line({{0, 0}, {1.5f, 0}, {1.5f, 3.0f}});
    CHECK_NEAR(pursue(p, u, 0, 0, 0).angular_velocity, 0.0, kTol); // (1.5,0) は正面
    // 全点が Ld より近ければ最後の点を目標にする
    const path_trajectory_t v = line({{0, 0}, {0.5f, 0}, {0.5f, 0.5f}});
    CHECK_NEAR(pursue(p, v, 0, 0, 0).angular_velocity, 2 * std::sin(kPi / 4) / std::sqrt(0.5), 1e-3);
    // Ld が極小でも 0.01 に丸めて動く
    path_pursuit_params_t tiny = p;
    tiny.lookahead_distance = 0.0f;
    CHECK(pursue(tiny, line({{0, 0}, {1.0f, 0}, {2.0f, 0}}), 0, 0, 0).linear_velocity > 0);
}

TEST_CASE(pursuit_ignores_waypoints_already_passed) {
    // 一直線の経路上を進行中 (5.2, 0)、θ=0.3 rad で経路方向から外れている。
    // 注視点は前方 (7,0) で、α = -0.3、距離 1.8 → ω = 2 sin(-0.3) / 1.8
    const path_pursuit_params_t p{1.0f, 1.0f, 10.0f, 0.1f};
    path_trajectory_t t{};
    for (int i = 0; i <= 10; ++i)
        t.points[t.count++] = {float(i), 0.0f};
    const path_control_cmd_t c = pursue(p, t, 5.2f, 0.0f, 0.3f);
    CHECK_NEAR(c.angular_velocity, 2 * std::sin(-0.3) / 1.8, 1e-3);
    // 経路に戻る向きに回る (経路は機体から見て右手 → 負の旋回)
    CHECK(c.angular_velocity < 0);
}

TEST_CASE(pursuit_distances_use_both_coordinates) {
    path_pursuit_params_t p{1.5f, 1.0f, 10.0f, 0.51f};
    // ゴール (3,4)。機体 (3.3,4.4) までの距離は 0.5 ≤ 0.51 → 到達。(3.3,4.5) は 0.583 → 未到達
    const path_trajectory_t g = line({{0, 0}, {3.0f, 4.0f}});
    CHECK(pursue(p, g, 3.3f, 4.4f, 0).is_goal_reached);
    CHECK(!pursue(p, g, 3.3f, 4.5f, 0).is_goal_reached);
    CHECK(pursue(p, g, 3.0f, 3.6f, 0).is_goal_reached); // y だけ離れている場合も
    CHECK(!pursue(p, g, 3.0f, 3.4f, 0).is_goal_reached);

    // 注視点までの距離にも y 成分が効く: 機体 (10,10)、注視点候補 (10,11) は 1.0 < 1.5 で手前、(10,11.6) は 1.6 ≥ 1.5
    const path_trajectory_t t = line({{10, 10}, {10.0f, 11.0f}, {10.0f, 11.6f}});
    const path_control_cmd_t c = pursue(p, t, 10.0f, 10.0f, 1.5707964f); // +y 向き
    CHECK_NEAR(c.angular_velocity, 0.0, 1e-3); // どちらも正面
    const path_trajectory_t u = line({{10, 10}, {10.5f, 10.5f}, {9.0f, 11.6f}});
    // (10.5,10.5) は 0.707 < 1.5 で手前。(9, 11.6) の距離 1.887 が採用される (右 → 左へ)
    const path_control_cmd_t d = pursue(p, u, 10.0f, 10.0f, 0.0f);
    const double alpha = std::atan2(1.6, -1.0);
    CHECK_NEAR(d.angular_velocity, 2 * std::sin(alpha) / std::hypot(1.0, 1.6), 1e-3);
}

TEST_CASE(pursuit_target_stays_within_the_trajectory) {
    // 全点が Ld より近いとき、目標は最後の点。配列の範囲外(未使用の原点)を選んではならない
    const path_pursuit_params_t p{1.5f, 1.0f, 10.0f, 0.05f};
    const path_trajectory_t t = line({{10, 10}, {10.2f, 10.0f}, {10.2f, 10.2f}});
    const path_control_cmd_t c = pursue(p, t, 10.0f, 10.0f, 0.0f);
    // 目標 (10.2,10.2): α = 45°、距離 0.2828 → κ = 2 sin45° / 0.2828 = 5
    CHECK_NEAR(c.angular_velocity, 5.0, 1e-2);

    // 最近点の探索も範囲内: 原点近くの機体と、遠い 2 点の経路 → 先頭の点 (5,3) が目標
    const path_pursuit_params_t q{1.0f, 1.0f, 100.0f, 0.05f};
    const path_trajectory_t far = line({{5.0f, 3.0f}, {6.0f, -1.0f}});
    const path_control_cmd_t d = pursue(q, far, 0.1f, 0.0f, 0.0f);
    const double alpha = std::atan2(3.0, 4.9);
    CHECK_NEAR(d.angular_velocity, 2 * std::sin(alpha) / std::hypot(4.9, 3.0), 1e-3);
    CHECK(d.angular_velocity > 0);
}

TEST_CASE(pursuit_nearest_point_uses_both_coordinates) {
    // 斜めの経路 (i, i)。機体 (5.2, 5.0) に最も近いのは (5,5)。そこから先で Ld=1 以上離れた最初の点 (6,6) が目標
    const path_pursuit_params_t p{1.0f, 1.0f, 10.0f, 0.1f};
    path_trajectory_t t{};
    for (int i = 0; i <= 10; ++i)
        t.points[t.count++] = {float(i), float(i)};
    const path_control_cmd_t c = pursue(p, t, 5.2f, 5.0f, 0.3f);
    const double alpha = std::atan2(1.0, 0.8) - 0.3;
    CHECK_NEAR(c.angular_velocity, 2 * std::sin(alpha) / std::hypot(0.8, 1.0), 1e-3);
}

TEST_CASE(pursuit_goal_detection_boundary_and_degenerate_inputs) {
    path_pursuit_params_t p{1.0f, 0.5f, 10.0f, 0.5f};
    const path_trajectory_t t = line({{0, 0}, {4.0f, 0}, {8.0f, 0}});
    // ゴール (8,0) までの距離が許容半径ちょうど(0.5)なら到達
    path_control_cmd_t c = pursue(p, t, 7.5f, 0, 0);
    CHECK(c.is_goal_reached);
    CHECK_NEAR(c.linear_velocity, 0.0, 1e-7);
    CHECK_NEAR(c.angular_velocity, 0.0, 1e-7);
    // 少し外側なら未到達
    c = pursue(p, t, 7.4f, 0, 0);
    CHECK(!c.is_goal_reached);
    CHECK_NEAR(c.linear_velocity, 0.5, kTol);

    // 経路が空 / 引数が null → 停止して到達扱い
    path_trajectory_t empty{};
    c = pursue(p, empty, 0, 0, 0);
    CHECK(c.is_goal_reached);
    CHECK_NEAR(c.linear_velocity, 0.0, 1e-7);
    path_control_cmd_t d{1, 1, false};
    path_pursuit_compute(nullptr, &t, 0, 0, 0, &d);
    CHECK(d.is_goal_reached);
    CHECK_NEAR(d.linear_velocity, 0.0, 1e-7);
    d = {1, 1, false};
    path_pursuit_compute(&p, nullptr, 0, 0, 0, &d);
    CHECK(d.is_goal_reached);
    path_pursuit_compute(&p, &t, 0, 0, 0, nullptr); // 落ちない
}

EMBED_TEST_MAIN()
