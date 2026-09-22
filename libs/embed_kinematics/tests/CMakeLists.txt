# embed_kinematics unit tests
add_executable(test_kinematics
    test_kinematics.cpp
)

target_link_libraries(test_kinematics
    PRIVATE
        embed_kinematics
)

target_set_warnings(test_kinematics)

add_test(NAME test_kinematics COMMAND test_kinematics)

target_link_libraries(test_kinematics PRIVATE embed_test)
