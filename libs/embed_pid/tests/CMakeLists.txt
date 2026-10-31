add_executable(test_pid
    test_pid.cpp
)

# 制御則・アンチワインドアップ・バンプレス転送・Ziegler-Nichols の表・閉ループ応答を理論値で固定するテスト
add_executable(test_pid_spec
    test_pid_spec.cpp
)

foreach(t test_pid test_pid_spec)
    target_link_libraries(${t} PRIVATE embed_pid embed_test)
    target_set_warnings(${t})
    add_test(NAME ${t} COMMAND ${t})
endforeach()
