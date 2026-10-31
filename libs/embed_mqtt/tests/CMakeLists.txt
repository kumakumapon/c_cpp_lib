add_executable(test_mqtt
    test_mqtt.cpp
)

# MQTT 3.1.1 のバイト列・Remaining Length の境界・クライアントの状態遷移を固定するテスト
add_executable(test_mqtt_spec
    test_mqtt_spec.cpp
)

foreach(t test_mqtt test_mqtt_spec)
    target_link_libraries(${t} PRIVATE embed_mqtt embed_test)
    target_set_warnings(${t})
    add_test(NAME ${t} COMMAND ${t})
endforeach()
