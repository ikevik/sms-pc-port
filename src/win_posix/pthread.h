/* The pthread calls the port uses, for the msvc-target build, over Win32 threads,
 * SRW locks and condition variables (win_posix.cpp). Only what the port needs:
 * joinable and detached threads, plain mutexes, condition variables.
 * pthread_attr_setstack records the stack but cannot place a thread on it (Windows
 * has no such call): the port runs its game threads on low stacks through its own
 * entry trampoline instead (src/port_win64_stack.h). */
#ifndef SMS_WIN_POSIX_PTHREAD_H
#define SMS_WIN_POSIX_PTHREAD_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include "win_posix.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uintptr_t pthread_t;

typedef struct {
	size_t stacksize;
	void* stackaddr;
	int detachstate;
} pthread_attr_t;

/* SRWLOCK and CONDITION_VARIABLE are one pointer each, and statically initialised to 0. */
typedef struct { void* p; } pthread_mutex_t;
typedef struct { void* p; } pthread_cond_t;

#define PTHREAD_MUTEX_INITIALIZER { 0 }
#define PTHREAD_COND_INITIALIZER { 0 }
#define PTHREAD_CREATE_JOINABLE 0
#define PTHREAD_CREATE_DETACHED 1

int pthread_attr_init(pthread_attr_t* attr);
int pthread_attr_destroy(pthread_attr_t* attr);
int pthread_attr_setstacksize(pthread_attr_t* attr, size_t size);
int pthread_attr_setstack(pthread_attr_t* attr, void* addr, size_t size);
int pthread_attr_setdetachstate(pthread_attr_t* attr, int state);

int pthread_create(pthread_t* thread, const pthread_attr_t* attr, void* (*start)(void*), void* arg);
int pthread_join(pthread_t thread, void** result);
int pthread_detach(pthread_t thread);
pthread_t pthread_self(void);
int pthread_equal(pthread_t a, pthread_t b);
__attribute__((noreturn)) void pthread_exit(void* result);

int pthread_mutex_init(pthread_mutex_t* m, const void* attr);
int pthread_mutex_destroy(pthread_mutex_t* m);
int pthread_mutex_lock(pthread_mutex_t* m);
int pthread_mutex_trylock(pthread_mutex_t* m);
int pthread_mutex_unlock(pthread_mutex_t* m);

int pthread_cond_init(pthread_cond_t* c, const void* attr);
int pthread_cond_destroy(pthread_cond_t* c);
int pthread_cond_signal(pthread_cond_t* c);
int pthread_cond_broadcast(pthread_cond_t* c);
int pthread_cond_wait(pthread_cond_t* c, pthread_mutex_t* m);
/* abstime is on CLOCK_REALTIME, as in POSIX */
int pthread_cond_timedwait(pthread_cond_t* c, pthread_mutex_t* m, const struct timespec* abstime);

#ifdef __cplusplus
}
#endif
#endif
