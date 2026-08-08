cmake_minimum_required(VERSION 3.24)

project(PadMirror VERSION 0.1.6 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(PADMIRROR_BUILD_APP "Build the Qt/GStreamer desktop application" ON)
option(PADMIRROR_BUILD_TESTS "Build core unit tests" ON)
option(PADMIRROR_ENABLE_IMOBILEDEVICE "Use libimobiledevice for trust and device metadata" ON)
set(PADMIRROR_LIBUSB_ROOT "" CACHE PATH "Path to a prebuilt libusb package for the non-Windows legacy backend")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(Platform)
include(Dependencies)

add_subdirectory(src)

if(PADMIRROR_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
