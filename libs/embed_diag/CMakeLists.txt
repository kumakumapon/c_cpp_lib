# embed_diag library
add_library(embed_diag
    src/embed_diag.c
)

target_include_directories(embed_diag
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_set_warnings(embed_diag)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
