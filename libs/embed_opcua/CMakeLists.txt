# embed_opcua library
add_library(embed_opcua
    src/opcua_binary.c
    src/opcua_server.c
)

target_include_directories(embed_opcua
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_set_warnings(embed_opcua)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
