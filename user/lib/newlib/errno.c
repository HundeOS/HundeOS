#include <errno.h>

int errno;

const char * strerror(int error_number)
{
    switch (error_number)
    {
    case EPERM:
        return "Operation not permitted";
    case ENOENT:
        return "No such file or directory";
    case EIO:
        return "Input/output error";
    case EBADF:
        return "Bad file descriptor";
    case EAGAIN:
        return "Resource temporarily unavailable";
    case ENOMEM:
        return "Cannot allocate memory";
    case EACCES:
        return "Permission denied";
    case EFAULT:
        return "Bad address";
    case EEXIST:
        return "File exists";
    case EXDEV:
        return "Cross-device link";
    case ENOTDIR:
        return "Not a directory";
    case EISDIR:
        return "Is a directory";
    case EINVAL:
        return "Invalid argument";
    case EMFILE:
        return "Too many open files";
    case ENOSYS:
        return "Function not implemented";
    case ENAMETOOLONG:
        return "File name too long";
    case EOPNOTSUPP:
        return "Operation not supported";
    default:
        return "Unknown error";
    }
}
