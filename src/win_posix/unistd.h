/* <unistd.h> for the msvc-target build. open/read/close/lseek/access come from <io.h>
 * (CMake defines _CRT_DECLARE_NONSTDC_NAMES); the rest is in win_posix.h. */
#ifndef SMS_WIN_POSIX_UNISTD_H
#define SMS_WIN_POSIX_UNISTD_H
#include <io.h>
#include <process.h>
#include <direct.h>
#include <stdlib.h>
#include <sys/types.h>
#include "win_posix.h"

#ifndef F_OK
#define F_OK 0
#define W_OK 2
#define R_OK 4
#endif
#ifndef STDIN_FILENO
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#endif
#endif
