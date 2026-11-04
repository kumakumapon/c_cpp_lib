# uart_protocol library
add_library(uart_protocol
    src/uart_crc.c
    src/uart_protocol.c
    src/uart_arq.c
)

target_include_directories(uart_protocol
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# 最大ペイロード長。構造体のサイズが変わるので、利用側にも同じ値を伝える (PUBLIC)。
set(UART_PROTO_MAX_PAYLOAD 512 CACHE STRING "uart_protocol: maximum payload length (1..65535)")
target_compile_definitions(uart_protocol PUBLIC UART_PROTO_MAX_PAYLOAD=${UART_PROTO_MAX_PAYLOAD})

target_set_warnings(uart_protocol)
target_link_libraries(uart_protocol PUBLIC embed_crc embed_ringbuf)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
