cmake_minimum_required(VERSION 3.24)

project(PadMirror VERSION 1.0.0 LANGUAGES C CXX RC)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(PADMIRROR_BUILD_APP "Build the Windows AirPlay receiver" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Platform)
include(Dependencies)

add_subdirectory(src)
