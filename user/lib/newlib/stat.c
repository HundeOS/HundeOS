#include <errno.h>
#include <stddef.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../sys/io.h"
#include "current_directory.h"
#include "posix_io.h"

enum
{
    POSIX_STAT_BLOCK_SIZE = 4096,
};

static mode_t creation_mask = 0022;

static void PosixStat__fill(struct stat * status, IOFileInfo info)
{
    *status = (struct stat){
        .st_dev = 1,
        .st_ino = info.identity,
        .st_mode = info.mode,
        .st_nlink = 1,
        .st_uid = info.user_id,
        .st_gid = info.group_id,
        .st_size = info.size,
        .st_blksize = POSIX_STAT_BLOCK_SIZE,
    };
}

static int PosixStat__path(ConstBStringPtr path, struct stat * status)
{
    match_result(
        IO__file_info(path),
        success(info)
        {
            PosixStat__fill(status, info);
            if (S_ISDIR(status->st_mode))
            {
                status->st_nlink = 2;
            }
            return 0;
        },
        error(code, message)
        {
            errno = code == IO_PERMISSION_DENIED ? EACCES : ENOENT;
            return -1;
        }
    );
}

int stat(const char * path, struct stat * status)
{
    if (status == NULL)
    {
        errno = EFAULT;
        return -1;
    }
    BString absolute;
    return PosixPath__absolute(path, &absolute) == 0 ? PosixStat__path(&absolute, status) : -1;
}

int lstat(const char * path, struct stat * status)
{
    return stat(path, status);
}

int fstat(int descriptor, struct stat * status)
{
    if (status == NULL)
    {
        errno = EFAULT;
        return -1;
    }
    if (descriptor >= STDIN_FILENO && descriptor <= STDERR_FILENO)
    {
        *status = (struct stat){.st_mode = S_IFCHR | 0600, .st_nlink = 1, .st_blksize = POSIX_STAT_BLOCK_SIZE};
        return 0;
    }
    if (PosixIO__native_handle(descriptor) < 0)
    {
        return -1;
    }
    match_optional(
        PosixIO__path(descriptor),
        some(path)
        {
            return PosixStat__path(&path, status);
        },
        none_do_nothing()
    );
    errno = EBADF;
    return -1;
}

int mkdir(const char * path, mode_t mode)
{
    BString absolute;
    if (PosixPath__absolute(path, &absolute) != 0)
    {
        return -1;
    }
    IOStatus result = IO__make_dir(&absolute);
    if (result != IO_OK)
    {
        errno = result == IO_PERMISSION_DENIED ? EACCES : result == IO_ALREADY_EXISTS ? EEXIST : EIO;
        return -1;
    }
    return 0;
}

mode_t umask(mode_t mask)
{
    mode_t previous = creation_mask;
    creation_mask = mask & 0777;
    return previous;
}

int chmod(const char * path, mode_t mode)
{
    struct stat status;
    return stat(path, &status);
}

int fchmod(int descriptor, mode_t mode)
{
    return PosixIO__native_handle(descriptor) < 0 ? -1 : 0;
}

int fchown(int descriptor, uid_t owner, gid_t group)
{
    if (PosixIO__native_handle(descriptor) < 0)
    {
        return -1;
    }
    errno = EPERM;
    return -1;
}

int fchflags(int descriptor, unsigned long flags)
{
    if (PosixIO__native_handle(descriptor) < 0)
    {
        return -1;
    }
    if (flags == 0)
    {
        return 0;
    }
    errno = EOPNOTSUPP;
    return -1;
}

int futimens(int descriptor, const struct timespec times[2])
{
    return PosixIO__native_handle(descriptor) < 0 ? -1 : 0;
}
