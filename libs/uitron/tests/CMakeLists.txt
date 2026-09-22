add_executable(test_uitron
    test_uitron.cpp
)

target_link_libraries(test_uitron
    PRIVATE
        uitron
)

target_set_warnings(test_uitron)

add_test(NAME test_uitron COMMAND test_uitron)

target_link_libraries(test_uitron PRIVATE embed_test)
