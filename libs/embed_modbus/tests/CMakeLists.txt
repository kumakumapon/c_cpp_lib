add_executable(test_modbus
    test_modbus.cpp
)

# 規格(MODBUS Application Protocol V1.1b3 / Serial Line V1.02)のバイト列と規定動作を固定するテスト
add_executable(test_modbus_spec
    test_modbus_spec.cpp
)

foreach(t test_modbus test_modbus_spec)
    target_link_libraries(${t} PRIVATE embed_modbus embed_test)
    target_set_warnings(${t})
    add_test(NAME ${t} COMMAND ${t})
endforeach()
