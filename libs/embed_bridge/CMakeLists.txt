# embed_bridge library
add_library(embed_bridge
    src/bridge_router.c
)

target_include_directories(embed_bridge
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(embed_bridge
    PUBLIC
        embed_opcua
        embed_eip
        embed_canopen
        embed_mqtt
        embed_modbus
)

target_set_warnings(embed_bridge)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

target_link_libraries(embed_bridge PUBLIC embed_modbus_tcp)
