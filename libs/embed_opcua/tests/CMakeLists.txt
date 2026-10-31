add_executable(test_opcua
    test_opcua.cpp
)

# UA Binary 符号化・NodeId / Variant / DataValue・TCP メッセージ・読み書きサービスの StatusCode を固定するテスト
add_executable(test_opcua_spec
    test_opcua_spec.cpp
)

foreach(t test_opcua test_opcua_spec)
    target_link_libraries(${t} PRIVATE embed_opcua embed_test)
    target_set_warnings(${t})
    add_test(NAME ${t} COMMAND ${t})
endforeach()
