add_executable(motion_demo
    src/main.cpp
)

target_link_libraries(motion_demo
    PRIVATE
        embed_motion
)

target_set_warnings(motion_demo)
