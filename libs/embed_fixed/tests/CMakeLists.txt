add_executable(test_embed_fixed test_embed_fixed.cpp)

# 飽和・切り捨て・端点、平方根と三角関数の誤差上限、アダプタの手計算値を固定するテスト
add_executable(test_fixed_spec test_fixed_spec.cpp)

foreach(t test_embed_fixed test_fixed_spec)
    target_link_libraries(${t} PRIVATE embed_fixed embed_test)
    target_set_warnings(${t})
    add_test(NAME ${t} COMMAND ${t})
endforeach()
