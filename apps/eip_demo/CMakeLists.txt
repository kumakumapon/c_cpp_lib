add_executable(eip_demo
    src/main.cpp
)

target_link_libraries(eip_demo
    PRIVATE
        embed_eip
)

target_set_warnings(eip_demo)
