add_executable(test_uart_protocol
    test_uart_protocol.cpp
)

target_link_libraries(test_uart_protocol
    PRIVATE
        uart_protocol
)

target_set_warnings(test_uart_protocol)

add_test(NAME test_uart_protocol COMMAND test_uart_protocol)

target_link_libraries(test_uart_protocol PRIVATE embed_test)
