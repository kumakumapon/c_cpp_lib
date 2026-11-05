# firmware_demo: スーパーループ型ファームウェアの骨格 (複数ライブラリの組み合わせ方の例)
add_executable(firmware_demo src/main.cpp)
target_link_libraries(firmware_demo PRIVATE
    embed_sched embed_selftest embed_crc embed_ringbuf embed_frame embed_json
    embed_cli embed_nvparam embed_input embed_filter)
target_set_warnings(firmware_demo)
