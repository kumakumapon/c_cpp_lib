# embed_eip library
add_library(embed_eip
    src/eip_encap.c
    src/eip_cip.c
    src/eip_adapter.c
)

target_include_directories(embed_eip
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_set_warnings(embed_eip)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
