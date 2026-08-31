/*
 * padmirror_msvc_prefix.h
 *
 * Force-included (cl /FI) into every uxplay-native translation unit before any
 * other header. UxPlay was written for MinGW/MSYS2, whose <sys/types.h> defines
 * POSIX types that MSVC lacks. This prefix supplies the small set the vendored
 * tree needs, and pins the WIN32 macro the code's platform guards test.
 *
 * PadMirror-authored glue; not derived from UxPlay.
 */
#ifndef PADMIRROR_MSVC_PREFIX_H
#define PADMIRROR_MSVC_PREFIX_H

#if defined(_MSC_VER)

/* UxPlay guards its Windows branches on WIN32 (not _WIN32). */
#ifndef WIN32
#define WIN32 1
#endif

/* MinGW's <sys/types.h> defines ssize_t; MSVC does not. Map to SSIZE_T. */
#include <BaseTsd.h>
#ifndef _SSIZE_T_DEFINED
#define _SSIZE_T_DEFINED
typedef SSIZE_T ssize_t;
#endif

/* Silence CRT warnings for the vendored C sources. */
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS 1
#endif
#ifndef _CRT_NONSTDC_NO_WARNINGS
#define _CRT_NONSTDC_NO_WARNINGS 1
#endif
#ifndef _WINSOCK_DEPRECATED_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS 1
#endif

#endif /* _MSC_VER */

#endif /* PADMIRROR_MSVC_PREFIX_H */
