# cli_demo: embed_cli による保守コンソールのデモ
add_executable(cli_demo src/main.cpp)
target_link_libraries(cli_demo PRIVATE embed_cli sample_math)
target_set_warnings(cli_demo)
