# embed_modbus library
add_library(embed_modbus
    src/modbus_crc.c
    src/modbus_slave.c
    src/modbus_master.c
    src/modbus_rtu.c
)

target_include_directories(embed_modbus
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_set_warnings(embed_modbus)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

target_link_libraries(embed_modbus PUBLIC embed_crc)
