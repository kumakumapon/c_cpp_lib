add_executable(test_motion
    test_motion.cpp
)

# 等加速度運動の解析解を固定するテスト
add_executable(test_motion_spec
    test_motion_spec.cpp
)

foreach(t test_motion test_motion_spec)
    target_link_libraries(${t} PRIVATE embed_motion embed_test)
    target_set_warnings(${t})
    add_test(NAME ${t} COMMAND ${t})
endforeach()
