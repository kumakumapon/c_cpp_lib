# embed_motion library
add_library(embed_motion
    src/motion_profile.c
)

target_include_directories(embed_motion
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_set_warnings(embed_motion)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
