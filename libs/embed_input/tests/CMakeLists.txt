# チャタリング除去と、クリック・ダブルクリック・長押し・リピートの判定時刻を時間軸で確かめる
add_executable(test_embed_input test_embed_input.cpp)
target_link_libraries(test_embed_input PRIVATE embed_input embed_test)
target_set_warnings(test_embed_input)
add_test(NAME test_embed_input COMMAND test_embed_input)
