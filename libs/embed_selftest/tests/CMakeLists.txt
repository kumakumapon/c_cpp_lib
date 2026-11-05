add_executable(test_embed_selftest test_embed_selftest.cpp)

# 故障を注入した仮想メモリでの検出能力・ROM 分割検査・時計 / ウォッチドッグの境界
add_executable(test_selftest_spec test_selftest_spec.cpp)

foreach(t test_embed_selftest test_selftest_spec)
    target_link_libraries(${t} PRIVATE embed_selftest embed_test)
    target_set_warnings(${t})
    add_test(NAME ${t} COMMAND ${t})
endforeach()
