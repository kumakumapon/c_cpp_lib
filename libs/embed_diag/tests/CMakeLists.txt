# embed_diag unit tests
add_executable(test_diag
    test_diag.cpp
)

target_link_libraries(test_diag
    PRIVATE
        embed_diag
)

target_set_warnings(test_diag)

add_test(NAME test_diag COMMAND test_diag)
