# embed_mqtt library
add_library(embed_mqtt
    src/mqtt_topic.c
    src/mqtt_packet.c
    src/mqtt_client.c
)

target_include_directories(embed_mqtt
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_set_warnings(embed_mqtt)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
