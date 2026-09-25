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

static int PosixPath__absolute(const char * path, BStringPtr result)
{
    if (path == NULL)
    {
        errno = EFAULT;
        return -1;
    }
    size_t length = strlen(path);
    if (length == 0)
    {
        errno = ENOENT;
        return -1;
    }
    if (length >= BSTRING_MAX_SIZE || (path[0] != '/' && length + 1 >= BSTRING_MAX_SIZE))
    {
        errno = ENAMETOOLONG;
        return -1;
    }
    BString argument = BString__new(path);
    *result = IO__path_from_arg(&argument);
    return 0;
}

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

int isatty(int descriptor)
{
    return descriptor >= STDIN_FILENO && descriptor <= STDERR_FILENO;
}

int readlink(const char * path, char * buffer, size_t size)
{
    errno = EINVAL;
    return -1;
}

uid_t getuid(void)
{
    return 0;
}

gid_t getgid(void)
{
    return 0;
}

char * getcwd(char * buffer, size_t size)
{
    if (buffer == NULL || size < 2)
    {
        errno = EINVAL;
        return NULL;
    }
    buffer[0] = '/';
    buffer[1] = '\0';
    return buffer;
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
    errno = ENOSYS;
    return -1;
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
