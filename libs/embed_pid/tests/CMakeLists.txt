add_executable(test_pid
    test_pid.cpp
)

target_link_libraries(test_pid
    PRIVATE
        embed_pid
)

target_set_warnings(test_pid)

add_test(NAME test_pid COMMAND test_pid)
