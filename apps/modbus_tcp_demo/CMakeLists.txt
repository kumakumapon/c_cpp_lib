# modbus_tcp_demo: 実際の TCP ソケットで動く Modbus TCP サーバ / クライアント
add_executable(modbus_tcp_demo src/main.cpp)
find_package(Threads REQUIRED)
target_link_libraries(modbus_tcp_demo PRIVATE embed_modbus_tcp Threads::Threads)
if(WIN32)
    target_link_libraries(modbus_tcp_demo PRIVATE ws2_32)
endif()
target_set_warnings(modbus_tcp_demo)
