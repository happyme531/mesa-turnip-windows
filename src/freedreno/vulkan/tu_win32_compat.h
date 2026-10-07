#ifndef TU_WIN32_COMPAT_H
#define TU_WIN32_COMPAT_H

#include <io.h>
#include <process.h>
#include <windows.h>
#include <errno.h>
#include "c11/threads.h"

typedef mtx_t pthread_mutex_t;
typedef cnd_t pthread_cond_t;
typedef int pthread_condattr_t;

#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 1
#endif

static inline int pthread_mutex_init(pthread_mutex_t *m, const void *) { return mtx_init(m, mtx_plain); }
static inline int pthread_mutex_lock(pthread_mutex_t *m) { return mtx_lock(m); }
static inline int pthread_mutex_unlock(pthread_mutex_t *m) { return mtx_unlock(m); }
static inline int pthread_mutex_destroy(pthread_mutex_t *m) { mtx_destroy(m); return 0; }
static inline int pthread_condattr_init(pthread_condattr_t *a) { *a = CLOCK_MONOTONIC; return 0; }
static inline int pthread_condattr_setclock(pthread_condattr_t *a, int clock) { *a = clock; return clock == CLOCK_MONOTONIC ? 0 : EINVAL; }
static inline int pthread_condattr_destroy(pthread_condattr_t *) { return 0; }
static inline int pthread_cond_init(pthread_cond_t *c, const pthread_condattr_t *) { return cnd_init(c); }
static inline int pthread_cond_broadcast(pthread_cond_t *c) { return cnd_broadcast(c); }
static inline int pthread_cond_destroy(pthread_cond_t *c) { cnd_destroy(c); return 0; }
static inline int getpagesize(void) { SYSTEM_INFO info; GetSystemInfo(&info); return info.dwPageSize; }

#endif
