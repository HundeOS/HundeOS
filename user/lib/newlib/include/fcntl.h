#ifndef __USER_LIB_NEWLIB_FCNTL_H
#define __USER_LIB_NEWLIB_FCNTL_H

#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR 2
#define O_ACCMODE 3
#define O_CREAT 0x0200
#define O_TRUNC 0x0400
#define O_NONBLOCK 0x0004
#define O_NOFOLLOW 0x0100

#define AT_FDCWD (-100)
#define AT_SYMLINK_FOLLOW 0x01
#define AT_SYMLINK_NOFOLLOW 0x02

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

int open(const char * path, int flags, ...);

#endif
