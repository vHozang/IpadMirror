if(NOT PADMIRROR_BUILD_APP)
    return()
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

add_subdirectory("${CMAKE_SOURCE_DIR}/third_party/libplist"
                 "${CMAKE_BINARY_DIR}/third_party/libplist")
add_subdirectory("${CMAKE_SOURCE_DIR}/third_party/uxplay-native"
                 "${CMAKE_BINARY_DIR}/third_party/uxplay-native")

add_library(padmirror_airplay STATIC
    airplay/UxPlayReceiver.cpp
)
target_include_directories(padmirror_airplay PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(padmirror_airplay PUBLIC padmirror_uxplay_native)
if(MSVC)
    target_compile_options(padmirror_airplay PRIVATE /W4 /permissive- /EHsc)
endif()

qt_add_executable(padmirror
    main.cpp
    app/MainWindow.cpp
    app/RuntimeDependencies.cpp
    platform/windows/PadMirror.rc
)
qt_add_resources(padmirror padmirror_resources
    PREFIX "/"
    BASE "${CMAKE_SOURCE_DIR}"
    FILES "${CMAKE_SOURCE_DIR}/assets/icons/PadMirror-256.png"
)
target_include_directories(padmirror PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(padmirror PRIVATE
    padmirror_airplay
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    PkgConfig::GSTREAMER
    user32
    shell32
)
if(MSVC)
    target_compile_options(padmirror PRIVATE /W4 /permissive- /EHsc)
endif()
set_target_properties(padmirror PROPERTIES
    WIN32_EXECUTABLE TRUE
    OUTPUT_NAME "PadMirror"
)

add_executable(padmirror_runtime_check
    runtime_check_main.cpp
    app/RuntimeDependencies.cpp
)
target_include_directories(padmirror_runtime_check PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(padmirror_runtime_check PRIVATE Qt6::Core PkgConfig::GSTREAMER)
if(MSVC)
    target_compile_options(padmirror_runtime_check PRIVATE /W4 /permissive- /EHsc)
endif()
set_target_properties(padmirror_runtime_check PROPERTIES
    OUTPUT_NAME "PadMirrorRuntimeCheck"
)

install(TARGETS padmirror padmirror_runtime_check RUNTIME DESTINATION .)
