# embed_log unit tests
add_executable(test_embed_log
    test_embed_log.cpp
)

# 書式・境界・ヘキサダンプ・バックエンド・ロック順序を固定するテスト
add_executable(test_embed_log_spec
    test_embed_log_spec.cpp
)

foreach(t test_embed_log test_embed_log_spec)
    target_link_libraries(${t} PRIVATE embed_log embed_test)
    target_set_warnings(${t})
    add_test(NAME ${t} COMMAND ${t})
endforeach()
