# embed_pid library
add_library(embed_pid
    src/pid_controller.c
)

target_include_directories(embed_pid
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_set_warnings(embed_pid)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
