# embed_kinematics unit tests
add_executable(test_kinematics
    test_kinematics.cpp
)

# 差動2輪のオドメトリ・メカナムの手計算値と往復・アームの解と到達限界を幾何学的な値で固定するテスト
add_executable(test_kinematics_spec
    test_kinematics_spec.cpp
)

foreach(t test_kinematics test_kinematics_spec)
    target_link_libraries(${t} PRIVATE embed_kinematics embed_test)
    target_set_warnings(${t})
    add_test(NAME ${t} COMMAND ${t})
endforeach()
