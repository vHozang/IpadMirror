/*
 * padmirror_win32_threads.h
 *
 * MSVC/Win32 replacement for the POSIX pthread primitives that UxPlay's
 * threads.h and raop_ntp.c depend on. PadMirror builds the vendored UxPlay
 * sources with cl.exe, which ships no <pthread.h>/<unistd.h>. This header maps
 * the small pthread subset actually used by the vendored code onto native Win32
 * synchronization objects:
 *
 *   pthread_mutex_t         -> CRITICAL_SECTION
 *   pthread_cond_t          -> CONDITION_VARIABLE
 *   pthread_t               -> HANDLE (via _beginthreadex)
 *   pthread_cond_timedwait  -> SleepConditionVariableCS with a relative timeout
 *   clock_gettime/gettimeofday/usleep -> Win32 equivalents
 *
 * Only the primitives referenced by the vendored tree are provided. This file
 * is PadMirror-authored glue (not derived from UxPlay) and carries no GPL
 * obligation of its own, but it is only compiled as part of the GPLv3 native
 * AirPlay library.
 */
#ifndef PADMIRROR_WIN32_THREADS_H
#define PADMIRROR_WIN32_THREADS_H

#if !defined(_WIN32)
#error "padmirror_win32_threads.h is only for the MSVC/Win32 build"
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
/* winsock2.h must precede windows.h (windows.h otherwise pulls the legacy
 * winsock.h) and it is what actually declares struct timeval, used by
 * pm_gettimeofday below and by several vendored callers that include this
 * shim (via threads.h) before compat.h brings in the sockets headers. */
#include <winsock2.h>
#include <windows.h>
#include <process.h> /* _beginthreadex */
#include <time.h>    /* struct timespec (C11, VS2015+) */
#include <stdlib.h>  /* malloc/free */

#ifdef __cplusplus
extern "C" {
#endif

/* ----- clocks / sleeps -------------------------------------------------- */

#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME 0
#endif

/* struct timespec exists in MSVC <time.h> since VS2015; clockid_t does not. */
typedef int pm_clockid_t;

static __inline int pm_clock_gettime(pm_clockid_t clk, struct timespec* ts)
{
    FILETIME ft;
    unsigned long long t;
    (void)clk;
    if (!ts) return -1;
    GetSystemTimeAsFileTime(&ft);
    t = ((unsigned long long)ft.dwHighDateTime << 32) | (unsigned long long)ft.dwLowDateTime;
    /* FILETIME is 100ns ticks since 1601-01-01; shift to Unix epoch. */
    t -= 116444736000000000ULL;
    ts->tv_sec = (long)(t / 10000000ULL);
    ts->tv_nsec = (long)((t % 10000000ULL) * 100ULL);
    return 0;
}
#define clock_gettime(clk, ts) pm_clock_gettime((clk), (ts))

struct pm_timeval { long tv_sec; long tv_usec; };
static __inline int pm_gettimeofday(struct timeval* tv, void* tz)
{
    struct timespec ts;
    (void)tz;
    if (!tv) return -1;
    pm_clock_gettime(CLOCK_REALTIME, &ts);
    tv->tv_sec = ts.tv_sec;
    tv->tv_usec = ts.tv_nsec / 1000;
    return 0;
}
#define gettimeofday(tv, tz) pm_gettimeofday((tv), (tz))

static __inline void pm_usleep(unsigned long usec)
{
    /* Millisecond resolution is sufficient for the vendored callers. */
    Sleep((DWORD)((usec + 999UL) / 1000UL));
}
#ifndef usleep
#define usleep(x) pm_usleep((unsigned long)(x))
#endif

/* ----- mutexes ---------------------------------------------------------- */

typedef CRITICAL_SECTION pthread_mutex_t;
typedef void             pthread_mutexattr_t;

static __inline int pthread_mutex_init(pthread_mutex_t* m, const pthread_mutexattr_t* a)
{
    (void)a;
    InitializeCriticalSection(m);
    return 0;
}
static __inline int pthread_mutex_lock(pthread_mutex_t* m)   { EnterCriticalSection(m); return 0; }
static __inline int pthread_mutex_unlock(pthread_mutex_t* m) { LeaveCriticalSection(m); return 0; }
static __inline int pthread_mutex_destroy(pthread_mutex_t* m){ DeleteCriticalSection(m); return 0; }

/* ----- condition variables --------------------------------------------- */

typedef CONDITION_VARIABLE pthread_cond_t;
typedef void               pthread_condattr_t;

static __inline int pthread_cond_init(pthread_cond_t* c, const pthread_condattr_t* a)
{
    (void)a;
    InitializeConditionVariable(c);
    return 0;
}
static __inline int pthread_cond_signal(pthread_cond_t* c)    { WakeConditionVariable(c); return 0; }
static __inline int pthread_cond_broadcast(pthread_cond_t* c) { WakeAllConditionVariable(c); return 0; }
static __inline int pthread_cond_destroy(pthread_cond_t* c)   { (void)c; return 0; }

static __inline int pthread_cond_wait(pthread_cond_t* c, pthread_mutex_t* m)
{
    return SleepConditionVariableCS(c, m, INFINITE) ? 0 : -1;
}
static __inline int pthread_cond_timedwait(pthread_cond_t* c, pthread_mutex_t* m,
                                           const struct timespec* abstime)
{
    struct timespec now;
    long long ms;
    if (!abstime) return pthread_cond_wait(c, m);
    pm_clock_gettime(CLOCK_REALTIME, &now);
    ms = (long long)(abstime->tv_sec - now.tv_sec) * 1000LL
       + (long long)(abstime->tv_nsec - now.tv_nsec) / 1000000LL;
    if (ms < 0) ms = 0;
    /* Returns 0 on timeout as well; callers re-check their predicate. */
    SleepConditionVariableCS(c, m, (DWORD)ms);
    return 0;
}

/* ----- threads ---------------------------------------------------------- */

typedef HANDLE pthread_t;
typedef void   pthread_attr_t;

typedef struct pm_thread_ctx_s {
    void* (*start)(void*);
    void* arg;
} pm_thread_ctx_t;

static unsigned __stdcall pm_thread_trampoline(void* p)
{
    pm_thread_ctx_t ctx = *(pm_thread_ctx_t*)p;
    free(p);
    ctx.start(ctx.arg);
    return 0;
}

static __inline int pthread_create(pthread_t* th, const pthread_attr_t* attr,
                                   void* (*start)(void*), void* arg)
{
    uintptr_t handle;
    pm_thread_ctx_t* ctx;
    (void)attr;
    if (!th || !start) return -1;
    ctx = (pm_thread_ctx_t*)malloc(sizeof(*ctx));
    if (!ctx) return -1;
    ctx->start = start;
    ctx->arg = arg;
    handle = _beginthreadex(NULL, 0, pm_thread_trampoline, ctx, 0, NULL);
    if (handle == 0) {
        free(ctx);
        return -1;
    }
    *th = (HANDLE)handle;
    return 0;
}

static __inline int pthread_join(pthread_t th, void** retval)
{
    if (retval) *retval = NULL;
    if (th == NULL) return 0;
    WaitForSingleObject(th, INFINITE);
    CloseHandle(th);
    return 0;
}

#ifdef __cplusplus
}
#endif

#endif /* PADMIRROR_WIN32_THREADS_H */
