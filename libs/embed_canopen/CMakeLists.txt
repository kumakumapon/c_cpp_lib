# embed_canopen library
add_library(embed_canopen
    src/canopen_od.c
    src/canopen_node.c
)

target_include_directories(embed_canopen
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_set_warnings(embed_canopen)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
