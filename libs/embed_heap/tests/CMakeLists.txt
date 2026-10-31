add_executable(test_embed_heap
    test_embed_heap.cpp
)

# 非重複・統合・統計の整合・分割条件・アライメント・realloc・不正な解放をモデルと突き合わせるテスト
add_executable(test_heap_spec
    test_heap_spec.cpp
)

foreach(t test_embed_heap test_heap_spec)
    target_link_libraries(${t} PRIVATE embed_heap embed_test)
    target_set_warnings(${t})
    add_test(NAME ${t} COMMAND ${t})
endforeach()
