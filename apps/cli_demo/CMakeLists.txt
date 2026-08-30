# cli_demo application
add_executable(cli_demo
    src/main.cpp
)

target_link_libraries(cli_demo
    PRIVATE
        sample_math
)

target_set_warnings(cli_demo)
