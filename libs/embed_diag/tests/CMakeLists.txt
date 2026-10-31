# embed_diag unit tests
add_executable(test_diag
    test_diag.cpp
)

# スタックのカナリア境界・CPU 使用率の算出・記録リングの上書き順序などを固定するテスト
add_executable(test_diag_spec
    test_diag_spec.cpp
)

foreach(t test_diag test_diag_spec)
    target_link_libraries(${t} PRIVATE embed_diag embed_test)
    target_set_warnings(${t})
    add_test(NAME ${t} COMMAND ${t})
endforeach()
