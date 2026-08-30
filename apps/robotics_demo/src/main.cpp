#include <iostream>
#include <iomanip>
#include <vector>
#include <cmath>

#include "embed_kinematics/embed_kinematics.hpp"
#include "embed_path/embed_path.hpp"
#include "embed_filter/embed_filter.hpp"
#include "embed_fsm/embed_fsm.hpp"
#include "embed_diag/embed_diag.hpp"

// ロボット状態ID
enum RobotState : fsm_state_id_t {
    STATE_ROBOT_SYSTEM = 1,
    STATE_IDLE         = 2,
    STATE_NAVIGATING   = 3,
    STATE_CHARGING     = 4,
    STATE_EMERGENCY    = 5
};

// イベントID
enum RobotEvent : fsm_event_id_t {
    EVT_NAV_START      = 10,
    EVT_NAV_COMPLETE   = 11,
    EVT_LOW_BATTERY    = 12,
    EVT_OBSTACLE_ALERT = 99
};

int main() {
    std::cout << "=====================================================" << std::endl;
    std::cout << " Autonomous Robot Navigation & Diagnostics Demo" << std::endl;
    std::cout << " (embed_fsm + embed_path + embed_kinematics + embed_filter + embed_diag)" << std::endl;
    std::cout << "=====================================================" << std::endl;

    // 1. 診断・レコーダーの初期化
    uint8_t task_stack[512];
    embed_diag::StackMonitor stack_mon(task_stack, sizeof(task_stack));
    embed_diag::CpuMonitor cpu_mon;
    embed_diag::Recorder recorder;
    recorder.log(DIAG_LEVEL_INFO, 0x0001, "Robot System Boot", 0);

    // 2. FSM 階層状態マシンの構築
    embed_fsm::StateMachine fsm;
    fsm.add_state(STATE_ROBOT_SYSTEM, FSM_STATE_NONE, "RobotSystem", nullptr, nullptr, [](fsm_machine_t *f, const fsm_event_t *e, void *) -> fsm_result_t {
        if (e->id == EVT_OBSTACLE_ALERT) {
            std::cout << "  [FSM Alert] Emergency Stop Triggered by Upper Layer!" << std::endl;
            fsm_transition_to(f, STATE_EMERGENCY);
            return FSM_RET_TRANSITION;
        }
        return FSM_RET_UNHANDLED;
    });

    fsm.add_state(STATE_IDLE, STATE_ROBOT_SYSTEM, "Idle",
                  [](fsm_machine_t*, void*){ std::cout << "  [FSM] Entering IDLE state" << std::endl; },
                  [](fsm_machine_t*, void*){ std::cout << "  [FSM] Leaving IDLE state" << std::endl; },
                  [](fsm_machine_t *f, const fsm_event_t *e, void *) -> fsm_result_t {
                      if (e->id == EVT_NAV_START) {
                          fsm_transition_to(f, STATE_NAVIGATING);
                          return FSM_RET_TRANSITION;
                      }
                      return FSM_RET_UNHANDLED;
                  });

    fsm.add_state(STATE_NAVIGATING, STATE_ROBOT_SYSTEM, "Navigating",
                  [](fsm_machine_t*, void*){ std::cout << "  [FSM] Entering NAVIGATING state" << std::endl; },
                  [](fsm_machine_t*, void*){ std::cout << "  [FSM] Leaving NAVIGATING state" << std::endl; },
                  [](fsm_machine_t *f, const fsm_event_t *e, void *) -> fsm_result_t {
                      if (e->id == EVT_NAV_COMPLETE) {
                          fsm_transition_to(f, STATE_CHARGING);
                          return FSM_RET_TRANSITION;
                      }
                      return FSM_RET_UNHANDLED;
                  });

    fsm.add_state(STATE_CHARGING, STATE_ROBOT_SYSTEM, "Charging",
                  [](fsm_machine_t*, void*){ std::cout << "  [FSM] Entering CHARGING state" << std::endl; });

    fsm.add_state(STATE_EMERGENCY, FSM_STATE_NONE, "Emergency",
                  [](fsm_machine_t*, void*){ std::cout << "  [FSM] Entering EMERGENCY state" << std::endl; });

    fsm.start(STATE_IDLE);

    // 3. A* 経路計画 (Grid Planner: 10x10, Resolution 0.5m)
    std::cout << "\n>>> 1. A* Path Planning (Avoid Obstacles from [0,0] to [4,4]) <<<" << std::endl;
    embed_path::GridPlanner planner(10, 10, 0.5f);
    // 障害物の配置
    planner.set_obstacle(2, 1);
    planner.set_obstacle(2, 2);
    planner.set_obstacle(2, 3);

    path_trajectory_t traj{};
    planner.plan(0, 0, 4, 4, traj);
    std::cout << "  Generated " << traj.count << " waypoints, Total Distance: "
              << std::fixed << std::setprecision(2) << traj.total_length << " m" << std::endl;

    // 4. Pure Pursuit 追従 & 差動2輪運動学シミュレーション
    std::cout << "\n>>> 2. Pure Pursuit & Differential Drive Simulation <<<" << std::endl;
    fsm.dispatch(EVT_NAV_START);

    embed_path::PurePursuit pursuit(0.6f, 0.4f, 1.5f, 0.15f); // Ld=0.6m, V=0.4m/s
    embed_kinematics::DiffDrive diff_drive(0.04f, 0.20f);       // R=4cm, L=20cm
    embed_filter::Kalman1D kf(0.0f, 1.0f, 0.01f, 0.1f);         // センサノイズ除去

    pose2d_t robot_pose{traj.points[0].x, traj.points[0].y, 0.0f};
    float sim_dt = 0.1f;
    float sim_time = 0.0f;
    bool reached = false;

    std::cout << "  Time [s] |  X [m]  |  Y [m]  | Theta [deg] | Left [rad/s] | Right [rad/s]" << std::endl;
    std::cout << "  ---------+---------+---------+-------------+--------------+--------------" << std::endl;

    for (int step = 0; step < 100 && !reached; ++step) {
        path_control_cmd_t cmd = pursuit.compute(traj, robot_pose.x, robot_pose.y, robot_pose.theta);
        if (cmd.is_goal_reached) {
            reached = true;
            break;
        }

        diff_wheel_speeds_t ws = diff_drive.inverse(cmd.linear_velocity, cmd.angular_velocity);
        diff_drive.update_odometry(robot_pose, ws.left * 0.04f * sim_dt, ws.right * 0.04f * sim_dt);

        if (step % 5 == 0) {
            std::cout << "  " << std::setw(8) << std::fixed << std::setprecision(1) << sim_time
                      << " | " << std::setw(7) << std::setprecision(2) << robot_pose.x
                      << " | " << std::setw(7) << std::setprecision(2) << robot_pose.y
                      << " | " << std::setw(11) << std::setprecision(1) << (robot_pose.theta * 180.0f / 3.14159265f)
                      << " | " << std::setw(12) << std::setprecision(1) << ws.left
                      << " | " << std::setw(12) << std::setprecision(1) << ws.right << std::endl;
        }

        sim_time += sim_dt;
        cpu_mon.update(150, 10000); // 1.5% CPU 負荷
    }

    fsm.dispatch(EVT_NAV_COMPLETE);

    // 5. 診断情報の取得
    std::cout << "\n>>> 3. System Diagnostics Report <<<" << std::endl;
    size_t free_b = 0, used_b = 0;
    stack_mon.check(free_b, used_b);
    std::cout << "  Stack High Water Mark: " << used_b << " / 512 bytes (Free: " << free_b << " bytes)" << std::endl;
    std::cout << "  Average CPU Load: " << std::fixed << std::setprecision(1) << cpu_mon.usage_percent() << " %" << std::endl;
    std::cout << "  Recorded Events: " << recorder.get_logs().size() << " entries logged" << std::endl;

    std::cout << "\n=====================================================" << std::endl;
    std::cout << " Robotics demonstration completed successfully!" << std::endl;
    std::cout << "=====================================================" << std::endl;
    return 0;
}
