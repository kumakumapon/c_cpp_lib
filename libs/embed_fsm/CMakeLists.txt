# embed_fsm library
add_library(embed_fsm
    src/embed_fsm.c
)

target_include_directories(embed_fsm
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_set_warnings(embed_fsm)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
