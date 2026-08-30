# embed_path library
add_library(embed_path
    src/embed_path.c
)

target_include_directories(embed_path
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_set_warnings(embed_path)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
