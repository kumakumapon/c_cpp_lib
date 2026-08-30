# embed_bridge unit tests
add_executable(test_bridge
    test_bridge.cpp
)

target_link_libraries(test_bridge
    PRIVATE
        embed_bridge
)

target_set_warnings(test_bridge)

add_test(NAME test_bridge COMMAND test_bridge)
