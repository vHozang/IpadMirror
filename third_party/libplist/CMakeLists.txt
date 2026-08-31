# libplist (C core) vendored for PadMirror's MSVC build.
#
# UxPlay's AirPlay RTSP handlers depend on libplist to parse/produce Apple
# property lists. Only a MinGW/UCRT64 build of libplist exists on the build
# host, which is ABI-incompatible with the MSVC PadMirror binary, so the C core
# is vendored and compiled directly with cl.exe.
#
# The C core has NO external dependencies (its own XML/binary/JSON/OpenStep
# writers; no libxml2). Only PACKAGE_VERSION must be provided, via the
# hand-written config.h in this directory. The C++ wrapper (.cpp files) is not
# vendored because UxPlay uses only the C API.
#
# License: LGPL-2.1-or-later (see COPYING.LESSER.txt). Compiled as a static
# library and linked into the (GPLv3) native AirPlay library, which is
# compatible with the product's move to GPL-3.0-or-later.

add_library(padmirror_libplist STATIC
    src/base64.c
    src/bplist.c
    src/bytearray.c
    src/common.c
    src/hashtable.c
    src/jplist.c
    src/jsmn.c
    src/oplist.c
    src/out-default.c
    src/out-limd.c
    src/out-plutil.c
    src/plist.c
    src/ptrarray.c
    src/time64.c
    src/xplist.c
    libcnary/cnary.c
    libcnary/node.c
    libcnary/node_list.c
)

# Pure-C library: keep Qt autogen off (inherited-on from the app build).
set_target_properties(padmirror_libplist PROPERTIES
    AUTOMOC OFF
    AUTORCC OFF
    AUTOUIC OFF)

# config.h lives here; headers under include/plist and src/ and libcnary/include.
target_include_directories(padmirror_libplist
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"          # config.h
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/libcnary/include"
)

target_compile_definitions(padmirror_libplist
    PUBLIC
        LIBPLIST_STATIC=1          # neutralize __declspec(dllimport) in plist.h
    PRIVATE
        HAVE_CONFIG_H=1
)

if(MSVC)
    # libplist is vendored verbatim; silence CRT deprecation noise and treat as C.
    target_compile_definitions(padmirror_libplist PRIVATE
        _CRT_SECURE_NO_WARNINGS=1
        _CRT_NONSTDC_NO_WARNINGS=1
    )
    target_compile_options(padmirror_libplist PRIVATE /W0)
endif()
