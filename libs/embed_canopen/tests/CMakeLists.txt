# embed_canopen unit tests
add_executable(test_canopen
    test_canopen.cpp
)

target_link_libraries(test_canopen
    PRIVATE
        embed_canopen
)

target_set_warnings(test_canopen)

add_test(NAME test_canopen COMMAND test_canopen)
