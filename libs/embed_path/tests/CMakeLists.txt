# embed_path unit tests
add_executable(test_path
    test_path.cpp
)

# 経路の妥当性・最短性と純追跡の数式を固定するテスト
add_executable(test_path_spec
    test_path_spec.cpp
)

foreach(t test_path test_path_spec)
    target_link_libraries(${t} PRIVATE embed_path embed_test)
    target_set_warnings(${t})
    add_test(NAME ${t} COMMAND ${t})
endforeach()
