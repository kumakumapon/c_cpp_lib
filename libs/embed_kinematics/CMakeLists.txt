# embed_kinematics library
add_library(embed_kinematics
    src/kinematics.c
)

target_include_directories(embed_kinematics
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_set_warnings(embed_kinematics)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
