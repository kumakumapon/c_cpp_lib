add_executable(test_bridge test_bridge.cpp)

# 各エンドポイントの読み書き・型変換・失敗の扱い、周期・変化検出・不感帯・MQTT 受信
add_executable(test_bridge_spec test_bridge_spec.cpp)

foreach(t test_bridge test_bridge_spec)
    target_link_libraries(${t} PRIVATE embed_bridge embed_test)
    target_set_warnings(${t})
    add_test(NAME ${t} COMMAND ${t})
endforeach()
