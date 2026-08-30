# robotics_demo application
add_executable(robotics_demo
    src/main.cpp
)

target_link_libraries(robotics_demo
    PRIVATE
        embed_kinematics
        embed_path
        embed_filter
        embed_fsm
        embed_diag
)

target_set_warnings(robotics_demo)
