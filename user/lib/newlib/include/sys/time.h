#ifndef __USER_LIB_NEWLIB_SYS_TIME_H
#define __USER_LIB_NEWLIB_SYS_TIME_H

#include <fcntl.h>
#include <sys/types.h>

int gettimeofday(struct timeval * value, void * timezone);
int utimensat(int descriptor, const char * path, const struct timespec times[2], int flags);

#endif
