add_executable(test_embed_heap
    test_embed_heap.cpp
)

target_link_libraries(test_embed_heap
    PRIVATE
        embed_heap
)

target_set_warnings(test_embed_heap)

add_test(NAME test_embed_heap COMMAND test_embed_heap)
