# API単体テスト(エラーコード・状態遷移)
add_executable(test_uitron
    test_uitron_api.cpp
)
# 複数タスクが協調動作するシナリオテスト
add_executable(test_uitron_scenarios
    test_uitron_scenarios.cpp
)
# μITRON 4.0 仕様に沿った引数検証・生成パラメータ・待ち行列・タイムアウトのテスト
add_executable(test_uitron_spec
    test_uitron_spec.cpp
)

foreach(t test_uitron test_uitron_scenarios test_uitron_spec)
    target_link_libraries(${t} PRIVATE uitron embed_test)
    target_set_warnings(${t})
    add_test(NAME ${t} COMMAND ${t})
endforeach()
