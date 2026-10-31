# embed_fsm unit tests
add_executable(test_fsm
    test_fsm.cpp
)

# enter / exit の呼び出し順序・バブルアップ・再遷移・不正入力をログの完全一致で固定するテスト
add_executable(test_fsm_spec
    test_fsm_spec.cpp
)

foreach(t test_fsm test_fsm_spec)
    target_link_libraries(${t} PRIVATE embed_fsm embed_test)
    target_set_warnings(${t})
    add_test(NAME ${t} COMMAND ${t})
endforeach()
