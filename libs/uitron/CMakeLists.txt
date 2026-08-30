# uitron RTOS API library
add_library(uitron
    src/uitron_core.c
    src/uitron_task.c
    src/uitron_sem.c
    src/uitron_flg.c
    src/uitron_mbx.c
    src/uitron_mpf.c
)

target_include_directories(uitron
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_set_warnings(uitron)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
