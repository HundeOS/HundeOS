#include <errno.h>
#include <sys/reent.h>

struct _reent _impure_data;
struct _reent * _impure_ptr = &_impure_data;

int * __errno(void)
{
    return &_REENT_ERRNO(_impure_ptr);
}

const char * strerror(int error_number)
{
    switch (error_number)
    {
    case EPERM:
        return "Operation not permitted";
    case ENOENT:
        return "No such file or directory";
    case ESRCH:
        return "No such process";
    case EIO:
        return "Input/output error";
    case E2BIG:
        return "Argument list too long";
    case ENOEXEC:
        return "Exec format error";
    case ECHILD:
        return "No child processes";
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
    case ENOTTY:
        return "Not a terminal";
    case EMFILE:
        return "Too many open files";
    case ERANGE:
        return "Result too large";
    case ENOSYS:
        return "Function not implemented";
    case ENAMETOOLONG:
        return "File name too long";
    case EILSEQ:
        return "Invalid or incomplete multibyte character";
    case EOPNOTSUPP:
        return "Operation not supported";
    default:
        return "Unknown error";
    }
}
