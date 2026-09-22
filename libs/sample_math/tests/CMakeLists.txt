add_executable(test_sample_math
    test_math.cpp
)

target_link_libraries(test_sample_math
    PRIVATE
        sample_math
)

target_set_warnings(test_sample_math)

add_test(NAME test_sample_math COMMAND test_sample_math)

target_link_libraries(test_sample_math PRIVATE embed_test)
