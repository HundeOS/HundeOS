#ifndef __USER_LIB_NEWLIB_SYS_TIME_H
#define __USER_LIB_NEWLIB_SYS_TIME_H

#include <fcntl.h>
#include <sys/types.h>

int utimensat(int descriptor, const char * path, const struct timespec times[2], int flags);

#endif
