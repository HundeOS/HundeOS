#ifndef __USER_LIB_NEWLIB_SYS_TYPES_H
#define __USER_LIB_NEWLIB_SYS_TYPES_H

#include <stddef.h>
#include <stdint.h>

typedef int64_t off_t;
typedef int64_t pid_t;
typedef int64_t ssize_t;
typedef int64_t time_t;
typedef int64_t suseconds_t;
typedef int64_t clock_t;
typedef uint64_t blkcnt_t;
typedef uint64_t blksize_t;
typedef uint64_t dev_t;
typedef uint64_t ino_t;
typedef uint64_t nlink_t;
typedef uint32_t uid_t;
typedef uint32_t gid_t;
typedef uint32_t mode_t;
typedef uint32_t u_int32_t;

struct timespec
{
    time_t tv_sec;
    long tv_nsec;
};

struct timeval
{
    time_t tv_sec;
    suseconds_t tv_usec;
};

#define __dead __attribute__((noreturn))
#define __BEGIN_DECLS
#define __END_DECLS

#endif
