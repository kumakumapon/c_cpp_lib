# embed_log library
add_library(embed_log
    src/embed_log.c
)

target_include_directories(embed_log
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_set_warnings(embed_log)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
