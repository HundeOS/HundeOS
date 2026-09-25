#ifndef __USER_LIB_NEWLIB_SYS_MOUNT_H
#define __USER_LIB_NEWLIB_SYS_MOUNT_H

#include <limits.h>

struct statfs
{
    unsigned long f_iosize;
    char f_mntonname[PATH_MAX];
};

int statfs(const char * path, struct statfs * status);
int fstatfs(int descriptor, struct statfs * status);

#endif
