# COBS (Cheshire & Baker / Wikipedia の例) と SLIP (RFC 1055) のバイト列、ストリーム復号、乱数往復
add_executable(test_embed_frame test_embed_frame.cpp)
target_link_libraries(test_embed_frame PRIVATE embed_frame embed_test)
target_set_warnings(test_embed_frame)
add_test(NAME test_embed_frame COMMAND test_embed_frame)
