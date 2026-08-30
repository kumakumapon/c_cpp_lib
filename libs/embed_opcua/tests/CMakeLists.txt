add_executable(test_opcua
    test_opcua.cpp
)

target_link_libraries(test_opcua
    PRIVATE
        embed_opcua
)

target_set_warnings(test_opcua)

add_test(NAME test_opcua COMMAND test_opcua)
