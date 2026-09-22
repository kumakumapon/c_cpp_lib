# embed_path unit tests
add_executable(test_path
    test_path.cpp
)

target_link_libraries(test_path
    PRIVATE
        embed_path
)

target_set_warnings(test_path)

add_test(NAME test_path COMMAND test_path)

target_link_libraries(test_path PRIVATE embed_test)
