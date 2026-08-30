add_executable(pid_demo
    src/main.cpp
)

target_link_libraries(pid_demo
    PRIVATE
        embed_pid
)

target_set_warnings(pid_demo)
