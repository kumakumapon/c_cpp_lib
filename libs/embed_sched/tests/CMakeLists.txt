add_executable(test_embed_sched test_embed_sched.cpp)

# 発火時刻・周期のずれなし・バケット衝突・時間ジャンプ・問い合わせ・タスク削除・予算の境界
add_executable(test_sched_spec test_sched_spec.cpp)

foreach(t test_embed_sched test_sched_spec)
    target_link_libraries(${t} PRIVATE embed_sched embed_test)
    target_set_warnings(${t})
    add_test(NAME ${t} COMMAND ${t})
endforeach()
