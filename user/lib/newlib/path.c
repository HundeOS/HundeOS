#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <limits.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../sys/io.h"
#include "current_directory.h"
#include "posix_io.h"

int access(const char * path, int mode)
{
    struct stat status;
    return stat(path, &status);
}

int unlink(const char * path)
{
    BString absolute;
    if (PosixPath__absolute(path, &absolute) != 0)
    {
        return -1;
    }
    IOStatus status = IO__remove_file(&absolute);
    if (status != IO_OK)
    {
        errno = status == IO_INVALID_PATH ? ENOENT : EIO;
        return -1;
    }
    return 0;
}

int rmdir(const char * path)
{
    return unlink(path);
}

int link(const char * source, const char * target)
{
    BString absolute_source;
    BString absolute_target;
    if (PosixPath__absolute(source, &absolute_source) != 0 || PosixPath__absolute(target, &absolute_target) != 0)
    {
        return -1;
    }
    IOStatus status = IO__link_file(&absolute_source, &absolute_target);
    if (status != IO_OK)
    {
        errno = status == IO_INVALID_PATH     ? ENOENT
                : status == IO_ALREADY_EXISTS ? EEXIST
                : status == IO_PERMISSION_DENIED ? EACCES
                                                 : EOPNOTSUPP;
        return -1;
    }
    return 0;
}

int linkat(int source_descriptor, const char * source, int target_descriptor, const char * target, int flags)
{
    if (source_descriptor != AT_FDCWD || target_descriptor != AT_FDCWD)
    {
        errno = ENOSYS;
        return -1;
    }
    return link(source, target);
}

int symlink(const char * source, const char * target)
{
    errno = EOPNOTSUPP;
    return -1;
}

int rename(const char * source, const char * target)
{
    struct stat status;
    if (stat(source, &status) != 0)
    {
        return -1;
    }
    errno = EXDEV;
    return -1;
}

int readlink(const char * path, char * buffer, size_t size)
{
    errno = EINVAL;
    return -1;
}

char * realpath(const char * path, char * resolved)
{
    BString absolute;
    if (resolved == NULL || PosixPath__absolute(path, &absolute) != 0)
    {
        errno = EINVAL;
        return NULL;
    }
    BString__copy_to(&absolute, resolved, PATH_MAX);
    return resolved;
}

int chown(const char * path, uid_t owner, gid_t group)
{
    errno = EPERM;
    return -1;
}

int lchown(const char * path, uid_t owner, gid_t group)
{
    return chown(path, owner, group);
}

int chflags(const char * path, unsigned long flags)
{
    if (flags == 0)
    {
        return 0;
    }
    errno = EOPNOTSUPP;
    return -1;
}

off_t lseek(int descriptor, off_t offset, int whence)
{
    IOHandle handle = PosixIO__native_handle(descriptor);
    if (handle < 0)
    {
        return -1;
    }
    IOSeekWhence io_whence;
    switch (whence)
    {
    case SEEK_SET:
        io_whence = IO_SEEK_SET;
        break;
    case SEEK_CUR:
        io_whence = IO_SEEK_CURRENT;
        break;
    case SEEK_END:
        io_whence = IO_SEEK_END;
        break;
    default:
        errno = EINVAL;
        return -1;
    }
    IOSeekResult reply = IO__seek(handle, offset, io_whence);
    if (reply.status != IO_OK)
    {
        errno = reply.status == IO_UNKNOWN_HANDLE ? EBADF : EINVAL;
        return -1;
    }
    return (off_t)reply.position;
}

int ftruncate(int descriptor, off_t length)
{
    errno = ENOSYS;
    return -1;
}

int fchmodat(int descriptor, const char * path, mode_t mode, int flags)
{
    if (descriptor != AT_FDCWD)
    {
        errno = ENOSYS;
        return -1;
    }
    return chmod(path, mode);
}

int chflagsat(int descriptor, const char * path, unsigned long flags, int options)
{
    if (descriptor != AT_FDCWD)
    {
        errno = ENOSYS;
        return -1;
    }
    return chflags(path, flags);
}

int utimensat(int descriptor, const char * path, const struct timespec times[2], int flags)
{
    struct stat status;
    if (descriptor != AT_FDCWD)
    {
        errno = ENOSYS;
        return -1;
    }
    return stat(path, &status);
}

int mkfifo(const char * path, mode_t mode)
{
    errno = EOPNOTSUPP;
    return -1;
}

int mknod(const char * path, mode_t mode, dev_t device)
{
    errno = EOPNOTSUPP;
    return -1;
}

void _exit(int code)
{
    exit(code);
}

char * basename(char * path)
{
    if (path == NULL || *path == '\0')
    {
        return ".";
    }
    size_t length = strlen(path);
    while (length > 1 && path[length - 1] == '/')
    {
        path[--length] = '\0';
    }
    char * separator = strrchr(path, '/');
    return separator == NULL ? path : separator + 1;
}
