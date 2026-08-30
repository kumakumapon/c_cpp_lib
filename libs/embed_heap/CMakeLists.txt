# embed_heap library
add_library(embed_heap
    src/embed_heap.c
)

target_include_directories(embed_heap
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_set_warnings(embed_heap)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
