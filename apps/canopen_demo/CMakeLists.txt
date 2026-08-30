# canopen_demo application
add_executable(canopen_demo
    src/main.cpp
)

target_link_libraries(canopen_demo
    PRIVATE
        embed_canopen
)

target_set_warnings(canopen_demo)
