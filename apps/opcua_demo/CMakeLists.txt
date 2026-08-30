# opcua_demo application
add_executable(opcua_demo
    src/main.cpp
)

target_link_libraries(opcua_demo
    PRIVATE
        embed_opcua
)

target_set_warnings(opcua_demo)
