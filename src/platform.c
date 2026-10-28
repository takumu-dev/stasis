#include "platform.h"

/* An SRW lock on Windows and a pthread mutex elsewhere, each initialised statically, so the lock
 * needs no setup before its first use. */

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static SRWLOCK lock = SRWLOCK_INIT;

void stasis_lock(void) { AcquireSRWLockExclusive(&lock); }

void stasis_unlock(void) { ReleaseSRWLockExclusive(&lock); }

#else

#include <pthread.h>

/* NOLINTNEXTLINE(misc-include-cleaner): pthread.h declares it, through a header of glibc's own */
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

/* Locking a default mutex fails only on misuse, such as locking it twice on one thread, which the
 * core never does. */

void stasis_lock(void) { (void)pthread_mutex_lock(&lock); }

void stasis_unlock(void) { (void)pthread_mutex_unlock(&lock); }

#endif
