/* POSIX names the port uses, for builds that target Windows with the MSVC runtime
 * (clang on x86_64-pc-windows-msvc, UWP included). Force-included into every
 * translation unit by CMake in that build only; MinGW and the other hosts have
 * these natively. Windows.h is deliberately not included here: the game's
 * sources get this header too. The functions are in win_posix.cpp. */
#ifndef SMS_WIN_POSIX_H
#define SMS_WIN_POSIX_H

#if defined(_WIN32) && defined(_MSC_VER)

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef _SSIZE_T_DEFINED
#define _SSIZE_T_DEFINED
typedef intptr_t ssize_t;
#endif
#ifndef _MODE_T_DEFINED
#define _MODE_T_DEFINED
typedef unsigned short mode_t;
#endif
typedef unsigned int useconds_t;

#ifndef PATH_MAX
#define PATH_MAX 260
#endif

#ifndef strcasecmp
#define strcasecmp _stricmp
#endif
#ifndef strncasecmp
#define strncasecmp _strnicmp
#endif

#ifndef S_ISREG
#define S_ISREG(m) (((m) & _S_IFMT) == _S_IFREG)
#endif
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#endif

#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME 0
#define CLOCK_MONOTONIC 1
#endif
typedef int clockid_t;

int clock_gettime(clockid_t clk, struct timespec* ts);
int nanosleep(const struct timespec* req, struct timespec* rem);
int usleep(useconds_t us);
unsigned int sleep(unsigned int seconds);

static __inline char* realpath(const char* path, char* resolved)
{
	char* out = resolved ? resolved : (char*)malloc(PATH_MAX);
	if (!out)
		return NULL;
	if (!_fullpath(out, path, PATH_MAX)) {
		if (!resolved)
			free(out);
		return NULL;
	}
	return out;
}

#ifdef __cplusplus
}
#endif

#endif /* _WIN32 && _MSC_VER */
#endif /* SMS_WIN_POSIX_H */
