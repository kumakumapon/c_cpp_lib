add_executable(test_embed_log
    test_embed_log.cpp
)

target_link_libraries(test_embed_log
    PRIVATE
        embed_log
)

target_set_warnings(test_embed_log)

add_test(NAME test_embed_log COMMAND test_embed_log)
