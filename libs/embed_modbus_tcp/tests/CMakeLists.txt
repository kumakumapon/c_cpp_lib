add_executable(test_embed_modbus_tcp test_embed_modbus_tcp.cpp)

# 仕様書の例によるバイト列・Unit ID・ストリーム分割・クライアント API・トランザクションのタイムアウト
add_executable(test_modbus_tcp_spec test_modbus_tcp_spec.cpp)

foreach(t test_embed_modbus_tcp test_modbus_tcp_spec)
    target_link_libraries(${t} PRIVATE embed_modbus_tcp embed_test)
    target_set_warnings(${t})
    add_test(NAME ${t} COMMAND ${t})
endforeach()
