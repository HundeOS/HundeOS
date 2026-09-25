#ifndef __USER_LIB_NEWLIB_UTIL_H
#define __USER_LIB_NEWLIB_UTIL_H

#include <stddef.h>
#include <sys/types.h>

#define FMT_SCALED_STRSIZE 7

char * getbsize(int * header_length, long * block_size);
const char * user_from_uid(uid_t user, int nouser);
const char * group_from_gid(gid_t group, int nogroup);
void strmode(mode_t mode, char * output);
char * fflagstostr(unsigned long flags);
int fmt_scaled(long long number, char * output);

#define howmany(value, unit) (((value) + ((unit) - 1)) / (unit))
#define major(device) ((unsigned int)(((device) >> 8) & 0xffU))
#define minor(device) ((unsigned int)((device) & 0xffU))

#endif
