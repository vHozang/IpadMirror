if(NOT PADMIRROR_BUILD_APP)
    return()
endif()

if(NOT WIN32)
    message(FATAL_ERROR "PadMirror supports Windows 10/11 x64 only")
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GSTREAMER REQUIRED IMPORTED_TARGET
    gstreamer-1.0>=1.22
    gstreamer-app-1.0
    gstreamer-audio-1.0
    gstreamer-video-1.0
)
