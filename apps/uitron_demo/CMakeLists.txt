# uitron_demo application
add_executable(uitron_demo
    src/main.cpp
)

target_link_libraries(uitron_demo
    PRIVATE
        uitron
)

target_set_warnings(uitron_demo)
