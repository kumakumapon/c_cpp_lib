# 行編集・履歴・補完・引用符の分割・コマンド実行・数値の解析を、端末への出力ごと確かめる
add_executable(test_embed_cli test_embed_cli.cpp)
target_link_libraries(test_embed_cli PRIVATE embed_cli embed_test)
target_set_warnings(test_embed_cli)
add_test(NAME test_embed_cli COMMAND test_embed_cli)
