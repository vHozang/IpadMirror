/*
 * config.h for building libplist as part of PadMirror with MSVC (cl.exe).
 *
 * Hand-written replacement for the autotools-generated config.h. The vendored
 * libplist C core has no external dependencies (its own XML/binary/JSON/OpenStep
 * writers, no libxml2). The HAVE_* macros below are intentionally LEFT UNDEFINED
 * for MSVC so that libplist uses its own portable fallbacks:
 *   - strndup:      local static definitions in jplist.c / oplist.c
 *   - memmem:       local definition in plist.c
 *   - gmtime_r /
 *     localtime_r:  fake_*_r fallbacks in time64.c
 *   - strptime:     sscanf-based fallback branch in xplist.c
 *   - tm_gmtoff /
 *     tm_zone:      struct tm on Windows has neither; code guards on these.
 *
 * Only PACKAGE_VERSION is required to be defined (plist.c #errors otherwise).
 */
#ifndef PADMIRROR_LIBPLIST_CONFIG_H
#define PADMIRROR_LIBPLIST_CONFIG_H

#define PACKAGE_VERSION "2.7.0-padmirror"

/* MSVC provides a conforming snprintf since VS2015; nothing to shim here. */

/* Silence MSVC CRT "deprecated" warnings for standard C functions used by
 * libplist (sscanf, strcpy, etc.). libplist is vendored verbatim. */
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS 1
#endif
#ifndef _CRT_NONSTDC_NO_WARNINGS
#define _CRT_NONSTDC_NO_WARNINGS 1
#endif

#endif /* PADMIRROR_LIBPLIST_CONFIG_H */
