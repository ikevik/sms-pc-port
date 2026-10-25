/* <dirent.h> for the msvc-target build, over FindFirstFileExW (win_posix.cpp).
 * Names are UTF-8. */
#ifndef SMS_WIN_POSIX_DIRENT_H
#define SMS_WIN_POSIX_DIRENT_H

#ifdef __cplusplus
extern "C" {
#endif

#define DT_UNKNOWN 0
#define DT_DIR 4
#define DT_REG 8

struct dirent {
	unsigned char d_type;
	char d_name[780];
};

struct sms_DIR;
typedef struct sms_DIR DIR;

DIR* opendir(const char* path);
struct dirent* readdir(DIR* dir);
int closedir(DIR* dir);

#ifdef __cplusplus
}
#endif
#endif
