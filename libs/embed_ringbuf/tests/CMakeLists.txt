add_executable(test_embed_ringbuf test_embed_ringbuf.cpp)

# 容量ごとの折り返し・満杯 / 空・複数バイト操作を std::deque のモデルと突き合わせるテスト
add_executable(test_ringbuf_spec test_ringbuf_spec.cpp)

# peek / skip / find / ゼロコピー API と、2 スレッドでの SPSC 動作
add_executable(test_ringbuf_api test_ringbuf_api.cpp)
find_package(Threads REQUIRED)
target_link_libraries(test_ringbuf_api PRIVATE Threads::Threads)

foreach(t test_embed_ringbuf test_ringbuf_spec test_ringbuf_api)
    target_link_libraries(${t} PRIVATE embed_ringbuf embed_test)
    target_set_warnings(${t})
    add_test(NAME ${t} COMMAND ${t})
endforeach()
