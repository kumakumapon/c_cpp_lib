# mqtt_demo application
add_executable(mqtt_demo
    src/main.cpp
)

target_link_libraries(mqtt_demo
    PRIVATE
        embed_mqtt
)

target_set_warnings(mqtt_demo)
