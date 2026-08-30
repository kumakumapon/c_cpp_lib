# uart_protocol_demo application
add_executable(uart_protocol_demo
    src/main.cpp
)

target_link_libraries(uart_protocol_demo
    PRIVATE
        uart_protocol
)

target_set_warnings(uart_protocol_demo)
