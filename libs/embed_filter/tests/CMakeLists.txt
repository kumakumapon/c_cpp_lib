# embed_filter unit tests
add_executable(test_filter
    test_filter.cpp
)

# 理論値(漸化式の手計算・幾何学的に決まる姿勢)を固定するテスト
add_executable(test_filter_spec
    test_filter_spec.cpp
)

foreach(t test_filter test_filter_spec)
    target_link_libraries(${t} PRIVATE embed_filter embed_test)
    target_set_warnings(${t})
    add_test(NAME ${t} COMMAND ${t})
endforeach()
