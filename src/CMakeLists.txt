add_library(padmirror_support STATIC
    support/ReceiverConfig.cpp
    support/ReceiverIdentity.cpp
)
target_include_directories(padmirror_support PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(padmirror_support PUBLIC
    PADMIRROR_VERSION="${PROJECT_VERSION}"
)
if(MSVC)
    target_compile_options(padmirror_support PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(padmirror_support PRIVATE -Wall -Wextra -Wpedantic)
endif()

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
target_link_libraries(padmirror_airplay PUBLIC padmirror_support padmirror_uxplay_native)
if(MSVC)
    target_compile_options(padmirror_airplay PRIVATE /W4 /permissive- /EHsc)
endif()

add_library(padmirror_app_support STATIC
    app/AirPlayController.cpp
    app/ApplicationSettings.cpp
    app/DiagnosticsDialog.cpp
    app/Logging.cpp
    app/MainWindow.cpp
    app/RuntimeDependencies.cpp
    app/SettingsDialog.cpp
)
target_include_directories(padmirror_app_support PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(padmirror_app_support PUBLIC
    padmirror_airplay
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    PkgConfig::GSTREAMER
)
if(WIN32)
    target_link_libraries(padmirror_app_support PUBLIC user32 shell32 iphlpapi)
endif()
if(MSVC)
    target_compile_options(padmirror_app_support PRIVATE /W4 /permissive- /EHsc)
endif()

qt_add_executable(padmirror_app
    main.cpp
    platform/windows/PadMirror.rc
)
qt_add_resources(padmirror_app padmirror_resources
    PREFIX "/"
    BASE "${CMAKE_SOURCE_DIR}"
    FILES "${CMAKE_SOURCE_DIR}/assets/icons/PadMirror-256.png"
)
target_link_libraries(padmirror_app PRIVATE padmirror_app_support)
target_compile_definitions(padmirror_app PRIVATE
    PADMIRROR_VERSION="${PROJECT_VERSION}"
)
if(MSVC)
    target_compile_options(padmirror_app PRIVATE /W4 /permissive- /EHsc)
endif()
set_target_properties(padmirror_app PROPERTIES
    WIN32_EXECUTABLE TRUE
    OUTPUT_NAME "PadMirror"
)

add_executable(padmirror_runtime_check
    runtime_check_main.cpp
    app/RuntimeDependencies.cpp
)
target_include_directories(padmirror_runtime_check PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(padmirror_runtime_check PRIVATE Qt6::Core PkgConfig::GSTREAMER)
target_compile_definitions(padmirror_runtime_check PRIVATE
    PADMIRROR_VERSION="${PROJECT_VERSION}"
)
if(MSVC)
    target_compile_options(padmirror_runtime_check PRIVATE /W4 /permissive- /EHsc)
endif()
set_target_properties(padmirror_runtime_check PROPERTIES
    OUTPUT_NAME "PadMirrorRuntimeCheck"
)

install(TARGETS padmirror_app padmirror_runtime_check RUNTIME DESTINATION .)
