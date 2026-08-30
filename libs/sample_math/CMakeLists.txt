# sample_math library
add_library(sample_math
    src/math_utils.cpp
)

target_include_directories(sample_math
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_set_warnings(sample_math)

# テストの追加
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
