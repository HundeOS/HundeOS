#ifndef __USER_LIB_NEWLIB_SYS_TIMES_H
#define __USER_LIB_NEWLIB_SYS_TIMES_H

#include <sys/types.h>

#define CLK_TCK 1000000L

struct tms
{
    clock_t tms_utime;
    clock_t tms_stime;
    clock_t tms_cutime;
    clock_t tms_cstime;
};

clock_t times(struct tms *buffer);

#endif
