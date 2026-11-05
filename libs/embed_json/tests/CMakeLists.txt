# 書き出しの文字列・エスケープ・構造の誤用検出と、RFC 8259 の文法に沿ったトークン化・値の取り出し
add_executable(test_embed_json test_embed_json.cpp)
target_link_libraries(test_embed_json PRIVATE embed_json embed_test)
target_set_warnings(test_embed_json)
add_test(NAME test_embed_json COMMAND test_embed_json)
