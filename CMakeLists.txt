cmake_minimum_required(VERSION 3.20)

project(
    c_cpp_lib
    VERSION 0.1.0
    LANGUAGES C CXX
)

# ---------------------------------------------------------
# C / C++ Standard Settings
# ---------------------------------------------------------
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------
# Output Directory Settings (ビルド成果物を build/bin, build/lib に集約)
# ---------------------------------------------------------
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

# ---------------------------------------------------------
# Global Options
# ---------------------------------------------------------
option(BUILD_SHARED_LIBS "Build shared libraries (.dll/.so) instead of static ones" OFF)

# ---------------------------------------------------------
# CMake Modules
# ---------------------------------------------------------
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerWarnings)

# ---------------------------------------------------------
# Testing
# ---------------------------------------------------------
include(CTest) # defines BUILD_TESTING option (ON by default)

# ---------------------------------------------------------
# Subdirectories
# ---------------------------------------------------------
# 1. Libraries (ライブラリ群)
add_subdirectory(libs)

# 2. Applications (アプリケーション群)
add_subdirectory(apps)
