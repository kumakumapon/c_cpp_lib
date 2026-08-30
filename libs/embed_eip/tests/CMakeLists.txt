add_executable(test_eip
    test_eip.cpp
)

target_link_libraries(test_eip
    PRIVATE
        embed_eip
)

target_set_warnings(test_eip)

add_test(NAME test_eip COMMAND test_eip)
