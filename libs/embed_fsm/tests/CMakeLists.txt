# embed_fsm unit tests
add_executable(test_fsm
    test_fsm.cpp
)

target_link_libraries(test_fsm
    PRIVATE
        embed_fsm
)

target_set_warnings(test_fsm)

add_test(NAME test_fsm COMMAND test_fsm)
