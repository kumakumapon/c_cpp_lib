# フラッシュの書き込み規則 (1 -> 0 のみ) を模したシミュレータで、あらゆる時点の電源断から復旧できることを確かめる
add_executable(test_embed_nvparam test_embed_nvparam.cpp)
target_link_libraries(test_embed_nvparam PRIVATE embed_nvparam embed_test)
target_set_warnings(test_embed_nvparam)
add_test(NAME test_embed_nvparam COMMAND test_embed_nvparam)
