#ifndef __USER_LIB_NEWLIB_ERR_H
#define __USER_LIB_NEWLIB_ERR_H

#include <sys/types.h>

void warn(const char * format, ...);
void err(int status, const char * format, ...);
void warnx(const char * format, ...);
void warnc(int code, const char * format, ...);
void errx(int status, const char * format, ...) __dead;
void errc(int status, int code, const char * format, ...) __dead;

#endif
