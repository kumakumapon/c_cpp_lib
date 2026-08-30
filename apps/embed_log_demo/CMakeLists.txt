# embed_log_demo application
add_executable(embed_log_demo
    src/main.cpp
)

target_link_libraries(embed_log_demo
    PRIVATE
        embed_log
)

target_set_warnings(embed_log_demo)
