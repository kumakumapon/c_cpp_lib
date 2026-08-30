#include <iostream>
#include <iomanip>
#include <cmath>

#include "embed_motion/motion_types.h"
#include "embed_motion/motion_profile.h"
#include "embed_motion/embed_motion.hpp"

int main() {
    std::cout << "=====================================================" << std::endl;
    std::cout << " Motion Profile & Trajectory (embed_motion) Demo" << std::endl;
    std::cout << "=====================================================" << std::endl;

    // -----------------------------------------------------------------
    // Scenario 1: 単軸 S字加減速 (S-Curve Profile) 軌道生成
    // -----------------------------------------------------------------
    std::cout << "\n>>> Scenario 1: S-Curve Profile Generation (0 mm -> 100 mm) <<<" << std::endl;
    embed_motion::SingleAxis axis(150.0f, 300.0f, 300.0f, 1500.0f); // Vmax=150mm/s, A=300mm/s^2, J=1500mm/s^3
    axis.plan(0.0f, 100.0f, MOTION_PROFILE_SCURVE);

    std::cout << "  Profile Total Duration: " << std::fixed << std::setprecision(3) << axis.total_time() << " s" << std::endl;
    std::cout << "  Time [s] | Position [mm] | Velocity [mm/s] | Accel [mm/s^2]" << std::endl;
    std::cout << "  ---------+---------------+-----------------+---------------" << std::endl;

    float dt = 0.05f;
    for (float t = 0.0f; t <= axis.total_time() + dt; t += dt) {
        motion_state_t st = axis.evaluate(t);
        std::cout << "  " << std::setw(8) << std::fixed << std::setprecision(2) << t
                  << " | " << std::setw(13) << std::setprecision(2) << st.position
                  << " | " << std::setw(15) << std::setprecision(2) << st.velocity
                  << " | " << std::setw(13) << std::setprecision(2) << st.acceleration << std::endl;
        if (st.is_finished) break;
    }

    // -----------------------------------------------------------------
    // Scenario 2: 3軸直交ロボット (XYZ) 直線補間移動
    // -----------------------------------------------------------------
    std::cout << "\n>>> Scenario 2: 3-Axis Robot Linear Interpolation <<<" << std::endl;
    std::cout << "  Moving from [0, 0, 0] to [120, 90, 50] mm..." << std::endl;

    embed_motion::MultiAxis<3> robot(200.0f, 400.0f, 400.0f); // 合成ベクトル Vmax=200mm/s, A=400mm/s^2
    std::array<float, 3> start{0.0f, 0.0f, 0.0f};
    std::array<float, 3> target{120.0f, 90.0f, 50.0f};
    robot.plan(start, target);

    std::cout << "  Total Trajectory Time: " << robot.total_time() << " s" << std::endl;
    std::cout << "  Time [s] |   X [mm]   |   Y [mm]   |   Z [mm]   | Vel [mm/s]" << std::endl;
    std::cout << "  ---------+------------+------------+------------+-----------" << std::endl;

    std::array<float, 3> cur_pos{}, cur_vel{};
    float sim_dt = 0.1f;
    float sim_t = 0.0f;
    bool finished = false;

    while (!finished) {
        finished = robot.step(sim_dt, cur_pos, cur_vel);
        float v_mag = std::sqrt(cur_vel[0]*cur_vel[0] + cur_vel[1]*cur_vel[1] + cur_vel[2]*cur_vel[2]);
        
        std::cout << "  " << std::setw(8) << std::fixed << std::setprecision(1) << sim_t
                  << " | " << std::setw(10) << std::setprecision(2) << cur_pos[0]
                  << " | " << std::setw(10) << std::setprecision(2) << cur_pos[1]
                  << " | " << std::setw(10) << std::setprecision(2) << cur_pos[2]
                  << " | " << std::setw(9) << std::setprecision(1) << v_mag << std::endl;
        sim_t += sim_dt;
    }

    std::cout << "\n=====================================================" << std::endl;
    std::cout << " embed_motion demonstration completed successfully!" << std::endl;
    std::cout << "=====================================================" << std::endl;
    return 0;
}
