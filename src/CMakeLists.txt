add_library(padmirror_core STATIC
    capture/usb/PacketParser.cpp
    capture/usb/QuickTimeProtocol.cpp
    diagnostics/FpsCounter.cpp
    media/AudioRingBuffer.cpp
    media/LiveEdgeController.cpp
)

target_include_directories(padmirror_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

if(MSVC)
    target_compile_options(padmirror_core PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(padmirror_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(NOT PADMIRROR_BUILD_APP)
    return()
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(PADMIRROR_APP_SOURCES
    main.cpp
    app/AppController.cpp
    app/Logging.cpp
    app/MainWindow.cpp
    app/RuntimeDependencies.cpp
    app/Settings.cpp
    capture/CaptureSession.cpp
    capture/usb/UsbTransport.cpp
    device/DeviceManager.cpp
    diagnostics/Metrics.cpp
    media/AudioPipeline.cpp
    media/LanPipeline.cpp
    media/MediaSession.cpp
    media/VideoPipeline.cpp
    platform/PlatformAudio.cpp
    platform/PlatformVideo.cpp
    network/LanReceiver.cpp
)

if(WIN32)
    list(APPEND PADMIRROR_APP_SOURCES
        platform/windows/PadMirror.rc
        platform/windows/WindowsPower.cpp
        platform/windows/WindowsVideo.cpp
        platform/windows/WasapiAudio.cpp
    )
elseif(APPLE)
    enable_language(OBJCXX)
    list(APPEND PADMIRROR_APP_SOURCES
        platform/macos/MacPower.mm
        platform/macos/MacVideo.mm
        platform/macos/CoreAudioOutput.mm
    )
else()
    list(APPEND PADMIRROR_APP_SOURCES platform/PlatformPower.cpp)
endif()

qt_add_executable(padmirror ${PADMIRROR_APP_SOURCES})

if(MSVC)
    target_compile_options(padmirror PRIVATE /W4 /permissive- /EHsc)
endif()

qt_add_resources(padmirror padmirror_ui
    PREFIX "/"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/.."
    FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/../ui/Main.qml"
        "${CMAKE_CURRENT_SOURCE_DIR}/../ui/PlayerView.qml"
        "${CMAKE_CURRENT_SOURCE_DIR}/../ui/WaitingView.qml"
        "${CMAKE_CURRENT_SOURCE_DIR}/../ui/SettingsView.qml"
        "${CMAKE_CURRENT_SOURCE_DIR}/../ui/DiagnosticsOverlay.qml"
        "${CMAKE_CURRENT_SOURCE_DIR}/../assets/icons/PadMirror-256.png"
)

target_include_directories(padmirror PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(padmirror PRIVATE
    padmirror_core
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::QuickWidgets
    Qt6::Widgets
    PkgConfig::GSTREAMER
)

if(PADMIRROR_LIBUSB_TARGET)
    target_link_libraries(padmirror PRIVATE ${PADMIRROR_LIBUSB_TARGET})
endif()

if(PADMIRROR_HAVE_IMOBILEDEVICE)
    target_compile_definitions(padmirror PRIVATE PADMIRROR_HAVE_IMOBILEDEVICE=1)
    target_link_libraries(padmirror PRIVATE PkgConfig::IMOBILEDEVICE)
endif()

if(WIN32)
    target_link_libraries(padmirror PRIVATE user32 dwmapi advapi32 setupapi shell32)
elseif(APPLE)
    target_link_libraries(padmirror PRIVATE
        "-framework IOKit"
        "-framework CoreFoundation"
    )
endif()

set_target_properties(padmirror PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
    OUTPUT_NAME "PadMirror"
)

if(WIN32)
    add_executable(padmirror_runtime_check
        runtime_check_main.cpp
        app/RuntimeDependencies.cpp
    )
    target_include_directories(padmirror_runtime_check PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_options(padmirror_runtime_check PRIVATE /W4 /permissive- /EHsc)
    target_link_libraries(padmirror_runtime_check PRIVATE
        Qt6::Core PkgConfig::GSTREAMER advapi32 setupapi)
    set_target_properties(padmirror_runtime_check PROPERTIES
        OUTPUT_NAME "PadMirrorRuntimeCheck"
    )
endif()

install(TARGETS padmirror
    BUNDLE DESTINATION .
    RUNTIME DESTINATION .
)
