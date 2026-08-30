#include <iostream>
#include <cassert>
#include <cmath>

#include "embed_path/path_types.h"
#include "embed_path/embed_path.h"
#include "embed_path/embed_path.hpp"

void test_astar_search() {
    std::cout << "[Test] Running A* Grid Planner tests..." << std::endl;
    // 10x10 グリッド、解像度 1.0m
    embed_path::GridPlanner planner(10, 10, 1.0f);

    // 壁を作成 (x=5, y=0〜7 に障害物)
    for (size_t y = 0; y <= 7; ++y) {
        planner.set_obstacle(5, y, true);
    }

    path_trajectory_t traj{};
    // Start (0, 0) -> Goal (8, 0)
    bool ok = planner.plan(0, 0, 8, 0, traj);
    assert(ok);
    assert(traj.count > 0);
    assert(traj.total_length > 8.0f); // 壁を迂回するため直線距離より長い

    // スタート点とゴール点の一致確認
    assert(fabsf(traj.points[0].x - 0.5f) < 0.01f);
    assert(fabsf(traj.points[0].y - 0.5f) < 0.01f);
    assert(fabsf(traj.points[traj.count - 1].x - 8.5f) < 0.01f);
    assert(fabsf(traj.points[traj.count - 1].y - 0.5f) < 0.01f);

    std::cout << "[Test] A* Grid Planner passed! (Waypoints: " << traj.count << ", Length: " << traj.total_length << "m)" << std::endl;
}

void test_pure_pursuit() {
    std::cout << "[Test] Running Pure Pursuit tests..." << std::endl;
    embed_path::PurePursuit pursuit(1.0f, 0.5f, 1.5f, 0.2f); // Ld=1.0m, V=0.5m/s

    path_trajectory_t traj{};
    traj.count = 4;
    traj.points[0] = {0.0f, 0.0f};
    traj.points[1] = {2.0f, 0.0f};
    traj.points[2] = {4.0f, 2.0f};
    traj.points[3] = {6.0f, 2.0f};

    // ロボットが (0, 0), theta = 0 のとき
    auto cmd = pursuit.compute(traj, 0.0f, 0.0f, 0.0f);
    assert(!cmd.is_goal_reached);
    assert(fabsf(cmd.linear_velocity - 0.5f) < 0.01f);
    assert(fabsf(cmd.angular_velocity) < 0.01f); // まっすぐ進む

    // ロボットがゴール付近 (5.9, 2.0) に達したとき
    cmd = pursuit.compute(traj, 5.9f, 2.0f, 0.0f);
    assert(cmd.is_goal_reached);
    assert(cmd.linear_velocity == 0.0f);

    std::cout << "[Test] Pure Pursuit passed!" << std::endl;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << " Running embed_path test suite..." << std::endl;
    std::cout << "========================================" << std::endl;

    test_astar_search();
    test_pure_pursuit();

    std::cout << "========================================" << std::endl;
    std::cout << " All embed_path tests PASSED!" << std::endl;
    std::cout << "========================================" << std::endl;
    return 0;
}
