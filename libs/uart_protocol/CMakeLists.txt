# uart_protocol library
add_library(uart_protocol
    src/uart_crc.c
    src/uart_ringbuf.c
    src/uart_protocol.c
)

target_include_directories(uart_protocol
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_set_warnings(uart_protocol)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

target_link_libraries(uart_protocol PUBLIC embed_crc embed_ringbuf)
