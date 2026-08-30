# bridge_demo application
add_executable(bridge_demo
    src/main.cpp
)

target_link_libraries(bridge_demo
    PRIVATE
        embed_bridge
        embed_opcua
        embed_eip
        embed_canopen
        embed_mqtt
        embed_modbus
)

target_set_warnings(bridge_demo)
