#ifndef __USER_LIB_NEWLIB_TIME_H
#define __USER_LIB_NEWLIB_TIME_H

#include <stddef.h>
#include <sys/types.h>

struct tm
{
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;
    int tm_year;
    int tm_wday;
    int tm_yday;
    int tm_isdst;
};

time_t time(time_t * value);
struct tm * localtime(const time_t * value);
size_t strftime(char * buffer, size_t size, const char * format, const struct tm * value);

#endif
