# modbus_demo application
add_executable(modbus_demo
    src/main.cpp
)

target_link_libraries(modbus_demo
    PRIVATE
        embed_modbus
)

target_set_warnings(modbus_demo)
