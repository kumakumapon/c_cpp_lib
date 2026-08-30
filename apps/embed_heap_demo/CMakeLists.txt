# embed_heap_demo application
add_executable(embed_heap_demo
    src/main.cpp
)

target_link_libraries(embed_heap_demo
    PRIVATE
        embed_heap
)

target_set_warnings(embed_heap_demo)
