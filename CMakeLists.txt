cmake_minimum_required(VERSION 3.24)

project(PadMirror VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(PADMIRROR_BUILD_APP "Build the Windows AirPlay receiver" ON)
option(PADMIRROR_BUILD_TESTS "Build PadMirror tests" ON)

if(PADMIRROR_BUILD_APP AND WIN32)
    enable_language(RC)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Platform)
include(Dependencies)

add_subdirectory(src)

if(PADMIRROR_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
