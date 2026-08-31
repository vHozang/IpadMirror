# UxPlay AirPlay/RAOP protocol and direct GStreamer renderers, linked in-process.
# Source baseline: https://github.com/FDH2/UxPlay @
# fd5192b06f0c2ab2f664b8ab778ac6f772e4d741 (GPL-3.0-or-later).

add_library(padmirror_uxplay_native STATIC
    lib/airplay_video.c
    lib/byteutils.c
    lib/compat.c
    lib/crypto.c
    lib/dnssd.c
    lib/fairplay_playfair.c
    lib/http_request.c
    lib/http_response.c
    lib/httpd.c
    lib/logger.c
    lib/mirror_buffer.c
    lib/netutils.c
    lib/pairing.c
    lib/raop.c
    lib/raop_buffer.c
    lib/raop_ntp.c
    lib/raop_rtp.c
    lib/raop_rtp_mirror.c
    lib/srp.c
    lib/utils.c
    lib/llhttp/api.c
    lib/llhttp/http.c
    lib/llhttp/llhttp.c
    lib/playfair/hand_garble.c
    lib/playfair/modified_md5.c
    lib/playfair/omg_hax.c
    lib/playfair/playfair.c
    lib/playfair/sap_hash.c
    lib/mdnsd/dnssd_mdnsd.c
    lib/mdnsd/mdnsd.c
    renderers/audio_renderer.c
    renderers/fdk_aac_eld_decoder.c
    renderers/video_renderer.c
)

target_include_directories(padmirror_uxplay_native
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/lib"
        "${CMAKE_CURRENT_SOURCE_DIR}/renderers"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/lib/llhttp"
        "${CMAKE_CURRENT_SOURCE_DIR}/lib/playfair"
        "${CMAKE_CURRENT_SOURCE_DIR}/lib/mdnsd"
        "${CMAKE_CURRENT_SOURCE_DIR}/msvc"
)

target_link_libraries(padmirror_uxplay_native PUBLIC
    padmirror_libplist
    PkgConfig::GSTREAMER
)

target_compile_definitions(padmirror_uxplay_native PUBLIC PLIST_210=1)

set(PADMIRROR_OPENSSL_ROOT "" CACHE PATH
    "Directory containing OpenSSL MSVC crypto.lib/ssl.lib and include/openssl")
set(_ssl_search_roots "${PADMIRROR_OPENSSL_ROOT}")
if(DEFINED ENV{GSTREAMER_ROOT_X86_64})
    list(APPEND _ssl_search_roots "$ENV{GSTREAMER_ROOT_X86_64}")
endif()
list(APPEND _ssl_search_roots
    "$ENV{LOCALAPPDATA}/Programs/gstreamer/1.0/msvc_x86_64"
    "C:/gstreamer/1.0/msvc_x86_64")

find_library(PADMIRROR_LIBCRYPTO NAMES crypto libcrypto
    PATHS ${_ssl_search_roots} PATH_SUFFIXES lib)
find_library(PADMIRROR_LIBSSL NAMES ssl libssl
    PATHS ${_ssl_search_roots} PATH_SUFFIXES lib)
find_path(PADMIRROR_OPENSSL_INCLUDE openssl/ssl.h
    PATHS ${_ssl_search_roots} PATH_SUFFIXES include)

if(NOT PADMIRROR_LIBCRYPTO OR NOT PADMIRROR_LIBSSL OR NOT PADMIRROR_OPENSSL_INCLUDE)
    message(FATAL_ERROR
        "UxPlay requires OpenSSL from the GStreamer MSVC bundle. "
        "Set PADMIRROR_OPENSSL_ROOT when it cannot be discovered.")
endif()

target_include_directories(padmirror_uxplay_native PRIVATE "${PADMIRROR_OPENSSL_INCLUDE}")
target_link_libraries(padmirror_uxplay_native PUBLIC
    "${PADMIRROR_LIBSSL}"
    "${PADMIRROR_LIBCRYPTO}"
)

if(WIN32)
    target_link_libraries(padmirror_uxplay_native PUBLIC ws2_32 iphlpapi)
endif()

set_target_properties(padmirror_uxplay_native PROPERTIES
    AUTOMOC OFF
    AUTORCC OFF
    AUTOUIC OFF
)

if(MSVC)
    target_compile_options(padmirror_uxplay_native PRIVATE
        /FI${CMAKE_CURRENT_SOURCE_DIR}/msvc/padmirror_msvc_prefix.h
        /W0
    )
    target_compile_definitions(padmirror_uxplay_native PRIVATE
        WIN32=1
        _CRT_SECURE_NO_WARNINGS=1
        _CRT_NONSTDC_NO_WARNINGS=1
        _WINSOCK_DEPRECATED_NO_WARNINGS=1
    )
endif()
