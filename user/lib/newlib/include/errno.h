#ifndef __USER_LIB_NEWLIB_ERRNO_H
#define __USER_LIB_NEWLIB_ERRNO_H

#define EPERM 1
#define ENOENT 2
#define ESRCH 3
#define EINTR 4
#define EIO 5
#define E2BIG 7
#define ENOEXEC 8
#define ECHILD 10
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
#define ENOTTY 25
#define EFBIG 27
#define ENOSPC 28
#define ESPIPE 29
#define EROFS 30
#define EMLINK 31
#define EPIPE 32
#define ERANGE 34
#define ENOSYS 38
#define ENAMETOOLONG 63
#define EILSEQ 84
#define EOPNOTSUPP 95

int * __errno(void);
#define errno (*__errno())

const char * strerror(int error_number);

#endif
