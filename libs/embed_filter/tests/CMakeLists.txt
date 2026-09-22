# embed_filter unit tests
add_executable(test_filter
    test_filter.cpp
)

target_link_libraries(test_filter
    PRIVATE
        embed_filter
)

target_set_warnings(test_filter)

add_test(NAME test_filter COMMAND test_filter)

target_link_libraries(test_filter PRIVATE embed_test)
