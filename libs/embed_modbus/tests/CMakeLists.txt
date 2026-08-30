add_executable(test_modbus
    test_modbus.cpp
)

target_link_libraries(test_modbus
    PRIVATE
        embed_modbus
)

target_set_warnings(test_modbus)

add_test(NAME test_modbus COMMAND test_modbus)
