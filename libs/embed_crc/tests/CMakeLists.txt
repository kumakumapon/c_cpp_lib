add_executable(test_embed_crc test_embed_crc.cpp)

# カタログの check / residue、データシートの実例、ストリーミング・不正引数
add_executable(test_crc_spec test_crc_spec.cpp)

foreach(t test_embed_crc test_crc_spec)
    target_link_libraries(${t} PRIVATE embed_crc embed_test)
    target_set_warnings(${t})
    add_test(NAME ${t} COMMAND ${t})
endforeach()

# ライブラリと逆のテーブル設定でソースを直接ビルドし、同じ仕様テストを通す。
# これで 1 回のビルドでビット演算版とテーブル版の両方を検証できる。
add_executable(test_crc_spec_alt test_crc_spec.cpp ../src/embed_crc.c)
target_include_directories(test_crc_spec_alt PRIVATE ../include)
target_link_libraries(test_crc_spec_alt PRIVATE embed_test)
target_set_warnings(test_crc_spec_alt)
if(NOT EMBED_CRC_TABLES)
    target_compile_definitions(test_crc_spec_alt PRIVATE EMBED_CRC_TABLES)
endif()
add_test(NAME test_crc_spec_alt COMMAND test_crc_spec_alt)
