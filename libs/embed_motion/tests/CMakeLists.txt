add_executable(test_motion
    test_motion.cpp
)

target_link_libraries(test_motion
    PRIVATE
        embed_motion
)

target_set_warnings(test_motion)

add_test(NAME test_motion COMMAND test_motion)

target_link_libraries(test_motion PRIVATE embed_test)
