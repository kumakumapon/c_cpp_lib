add_executable(test_eip
    test_eip.cpp
)

# カプセル化・EPATH・CIP の固定バイト列、セッション管理、Identity / Assembly の一般ステータスを固定するテスト
add_executable(test_eip_spec
    test_eip_spec.cpp
)

foreach(t test_eip test_eip_spec)
    target_link_libraries(${t} PRIVATE embed_eip embed_test)
    target_set_warnings(${t})
    add_test(NAME ${t} COMMAND ${t})
endforeach()
