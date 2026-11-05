add_executable(test_canopen test_canopen.cpp)

# CiA 301 のフレーム配置: NMT・ブートアップ・SDO (加速 / 分割・アボート)・PDO・SYNC・EMCY・Heartbeat
add_executable(test_canopen_spec test_canopen_spec.cpp)

foreach(t test_canopen test_canopen_spec)
    target_link_libraries(${t} PRIVATE embed_canopen embed_test)
    target_set_warnings(${t})
    add_test(NAME ${t} COMMAND ${t})
endforeach()
