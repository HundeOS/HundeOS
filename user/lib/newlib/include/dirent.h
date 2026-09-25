#ifndef __USER_LIB_NEWLIB_DIRENT_H
#define __USER_LIB_NEWLIB_DIRENT_H

#include <limits.h>
#include <sys/types.h>

#define DT_UNKNOWN 0
#define DT_FIFO 1
#define DT_CHR 2
#define DT_DIR 4
#define DT_BLK 6
#define DT_REG 8
#define DT_LNK 10
#define DT_SOCK 12

struct dirent
{
    ino_t d_ino;
    unsigned char d_type;
    char d_name[PATH_MAX];
};

typedef struct DIR DIR;

DIR * opendir(const char * path);
struct dirent * readdir(DIR * directory);
int closedir(DIR * directory);

#endif
