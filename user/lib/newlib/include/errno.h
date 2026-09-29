#ifndef __USER_LIB_NEWLIB_ERRNO_H
#define __USER_LIB_NEWLIB_ERRNO_H

#define EPERM 1
#define ENOENT 2
#define EINTR 4
#define EIO 5
#define EBADF 9
#define EAGAIN 11
#define ENOMEM 12
#define EACCES 13
#define EFAULT 14
#define EEXIST 17
#define EXDEV 18
#define ENOTDIR 20
#define EISDIR 21
#define EINVAL 22
#define EMFILE 24
#define EFBIG 27
#define ENOSPC 28
#define ESPIPE 29
#define EROFS 30
#define EMLINK 31
#define EPIPE 32
#define ENOSYS 38
#define ENAMETOOLONG 63
#define EOPNOTSUPP 95

extern int errno;

const char * strerror(int error_number);

#endif
