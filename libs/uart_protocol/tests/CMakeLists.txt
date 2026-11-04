add_executable(test_uart_protocol test_uart_protocol.cpp)

# フレームのバイト列・全エラー経路・再同期・タイムアウト・統計・C++ ラッパー
add_executable(test_uart_protocol_spec test_uart_protocol_spec.cpp)

# ACK 待ち再送 (ARQ) を、損失・重複を注入する仮想回線で検証
add_executable(test_uart_arq test_uart_arq.cpp)

foreach(t test_uart_protocol test_uart_protocol_spec test_uart_arq)
    target_link_libraries(${t} PRIVATE uart_protocol embed_test)
    target_set_warnings(${t})
    add_test(NAME ${t} COMMAND ${t})
endforeach()
