add_executable(test_mqtt
    test_mqtt.cpp
)

target_link_libraries(test_mqtt
    PRIVATE
        embed_mqtt
)

target_set_warnings(test_mqtt)

add_test(NAME test_mqtt COMMAND test_mqtt)
