if(NOT PADMIRROR_BUILD_APP)
    return()
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS
    Core
    Gui
    Network
    Qml
    Quick
    QuickControls2
    QuickWidgets
    Widgets
)

find_package(PkgConfig REQUIRED)

pkg_check_modules(GSTREAMER REQUIRED IMPORTED_TARGET
    gstreamer-1.0>=1.22
    gstreamer-app-1.0
    gstreamer-audio-1.0
    gstreamer-video-1.0
)

if(PADMIRROR_LIBUSB_ROOT)
    set(_padmirror_libusb_dll "${PADMIRROR_LIBUSB_ROOT}/VS2022/MS64/dll/libusb-1.0.dll")
    set(_padmirror_libusb_lib "${PADMIRROR_LIBUSB_ROOT}/VS2022/MS64/dll/libusb-1.0.lib")
    set(_padmirror_libusb_include "${PADMIRROR_LIBUSB_ROOT}/include")
    if(NOT EXISTS "${_padmirror_libusb_dll}" OR
       NOT EXISTS "${_padmirror_libusb_lib}" OR
       NOT EXISTS "${_padmirror_libusb_include}/libusb.h")
        message(FATAL_ERROR "PADMIRROR_LIBUSB_ROOT does not contain the VS2022 x64 libusb package")
    endif()
    add_library(padmirror_libusb SHARED IMPORTED)
    set_target_properties(padmirror_libusb PROPERTIES
        IMPORTED_LOCATION "${_padmirror_libusb_dll}"
        IMPORTED_IMPLIB "${_padmirror_libusb_lib}"
        INTERFACE_INCLUDE_DIRECTORIES "${_padmirror_libusb_include}"
    )
    set(PADMIRROR_LIBUSB_TARGET padmirror_libusb)
    set(PADMIRROR_LIBUSB_RUNTIME "${_padmirror_libusb_dll}" CACHE INTERNAL "libusb runtime DLL")
else()
    find_package(unofficial-libusb CONFIG QUIET)
endif()

if(NOT PADMIRROR_LIBUSB_TARGET AND TARGET unofficial-libusb::libusb)
    set(PADMIRROR_LIBUSB_TARGET unofficial-libusb::libusb)
elseif(NOT PADMIRROR_LIBUSB_TARGET)
    pkg_check_modules(LIBUSB REQUIRED IMPORTED_TARGET libusb-1.0>=1.0.26)
    set(PADMIRROR_LIBUSB_TARGET PkgConfig::LIBUSB)
endif()

if(PADMIRROR_ENABLE_IMOBILEDEVICE)
    pkg_check_modules(IMOBILEDEVICE QUIET IMPORTED_TARGET libimobiledevice-1.0)
    if(IMOBILEDEVICE_FOUND)
        set(PADMIRROR_HAVE_IMOBILEDEVICE ON CACHE INTERNAL "libimobiledevice available")
    else()
        message(WARNING "libimobiledevice was not found; USB capture still builds, but trust metadata is limited")
    endif()
endif()
