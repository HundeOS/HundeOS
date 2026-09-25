#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>

#include "../../common/types/map.h"
#include "../sys/io.h"
#include "posix_io.h"

enum
{
    POSIX_FIRST_FILE_DESCRIPTOR = 3,
    POSIX_MAX_OPEN_FILES = 64,
};

static bool PosixIO__descriptor_equals(void * left, void * right)
{
    return *(int *)left == *(int *)right;
}

OptionalDef(int);
typedef struct PosixFileDescriptor
{
    IOHandle handle;
    BString path;
} PosixFileDescriptor;
OptionalDef(PosixFileDescriptor);
MapDefine(int, PosixFileDescriptor, POSIX_MAX_OPEN_FILES, PosixFileDescriptors, PosixIO__descriptor_equals);
MapBodyDefine(PosixFileDescriptors);

static PosixFileDescriptors descriptors = EmptyMap(PosixFileDescriptors);

static int PosixIO__errno(IOStatus status)
{
    switch (status)
    {
    case IO_NO_DEVICE_FOUND:
    case IO_INVALID_PATH:
    case IO_PATH_UNMANAGED:
        return ENOENT;
    case IO_NOT_OPEN:
    case IO_UNKNOWN_HANDLE:
        return EBADF;
    case IO_WAIT_FOR_DATA:
        return EAGAIN;
    case IO_INTERRUPTED:
        return EINTR;
    case IO_ALREADY_EXISTS:
        return EEXIST;
    case IO_OPERATION_NOT_IMPLEMENTED:
    case IO_OPERATION_NOT_SUPPORTED:
        return ENOSYS;
    case IO_PERMISSION_DENIED:
        return EACCES;
    default:
        return EIO;
    }
}

static bool PosixIO__is_standard_descriptor(int descriptor)
{
    return descriptor >= STDIN_FILENO && descriptor <= STDERR_FILENO;
}

IOHandle PosixIO__native_handle(int descriptor)
{
    if (PosixIO__is_standard_descriptor(descriptor))
    {
        return descriptor;
    }

    match_optional(
        PosixFileDescriptors__get(&descriptors, descriptor),
        some(file)
        {
            return file.handle;
        },
        none_do_nothing()
    );

    errno = EBADF;
    return IO_UNKNOWN_HANDLE;
}

Optional(BString) PosixIO__path(int descriptor)
{
    match_optional(
        PosixFileDescriptors__get(&descriptors, descriptor),
        some(file)
        {
            return Some(BString, file.path);
        },
        none_do_nothing()
    );
    return None(BString);
}

static bool PosixIO__descriptor_is_open(int descriptor)
{
    match_optional(
        PosixFileDescriptors__get(&descriptors, descriptor),
        some(handle)
        {
            return true;
        },
        none_do_nothing()
    );
    return false;
}

static int PosixIO__allocate_descriptor(IOHandle handle, ConstBStringPtr path)
{
    int descriptor = POSIX_FIRST_FILE_DESCRIPTOR;
    while (PosixIO__descriptor_is_open(descriptor))
    {
        descriptor++;
    }

    match_result(
        PosixFileDescriptors__set(&descriptors, descriptor, (PosixFileDescriptor){.handle = handle, .path = *path}),
        success(index)
        {
            return descriptor;
        },
        error(code, message)
        {
            errno = EMFILE;
            return -1;
        }
    );
}

int open(const char * path, int flags, ...)
{
    if (path == NULL)
    {
        errno = EFAULT;
    }
    size_t path_length = strlen(path);
    if (path_length >= BSTRING_MAX_SIZE || (path[0] != '/' && path_length + 1 >= BSTRING_MAX_SIZE))
    {
        errno = ENAMETOOLONG;
        return -1;
    }

    Flags io_flags;
    switch (flags & O_ACCMODE)
    {
    case O_RDONLY:
        io_flags = IO_OPEN_READ;
        break;
    case O_WRONLY:
        io_flags = IO_OPEN_WRITE;
        break;
    case O_RDWR:
        io_flags = IO_OPEN_READ | IO_OPEN_WRITE;
        break;
    default:
        errno = EINVAL;
        return -1;
    }

    BString argument = BString__new(path);
    BString absolute_path = IO__path_from_arg(&argument);
    if ((flags & O_TRUNC) != 0 && (io_flags & IO_OPEN_WRITE) != 0)
    {
        IOStatus remove_status = IO__remove_file(&absolute_path);
        if (remove_status != IO_OK && remove_status != IO_INVALID_PATH)
        {
            errno = PosixIO__errno(remove_status);
            return -1;
        }
    }
    IOHandle handle = IO__open(&absolute_path, io_flags);
    if (handle < 0)
    {
        errno = PosixIO__errno((IOStatus)handle);
        return -1;
    }

    int descriptor = PosixIO__allocate_descriptor(handle, &absolute_path);
    if (descriptor < 0)
    {
        IO__close(handle);
    }
    return descriptor;
}

ssize_t read(int descriptor, void * buffer, size_t size)
{
    if (buffer == NULL && size != 0)
    {
        errno = EFAULT;
        return -1;
    }

    IOHandle handle = PosixIO__native_handle(descriptor);
    if (handle < 0)
    {
        return -1;
    }

    ByteCount result = IO__read_bytes(handle, buffer, size);
    if (result == IO_EOF)
    {
        return 0;
    }
    if (result < 0)
    {
        errno = PosixIO__errno((IOStatus)result);
        return -1;
    }
    return result;
}

ssize_t write(int descriptor, const void * buffer, size_t size)
{
    if (buffer == NULL && size != 0)
    {
        errno = EFAULT;
        return -1;
    }

    IOHandle handle = PosixIO__native_handle(descriptor);
    if (handle < 0)
    {
        return -1;
    }

    ByteCount result = IO__write_bytes(handle, buffer, size);
    if (result < 0)
    {
        errno = PosixIO__errno((IOStatus)result);
        return -1;
    }
    return result;
}

int close(int descriptor)
{
    IOHandle handle = PosixIO__native_handle(descriptor);
    if (handle < 0)
    {
        return -1;
    }

    IOStatus status = IO__close(handle);
    if (status != IO_OK)
    {
        errno = PosixIO__errno(status);
        return -1;
    }

    if (!PosixIO__is_standard_descriptor(descriptor))
    {
        match_result(
            PosixFileDescriptors__delete(&descriptors, descriptor),
            success_do_nothing(),
            error(code, message)
            {
                errno = EIO;
                return -1;
            }
        );
    }
    return 0;
}

int fsync(int descriptor)
{
    if (PosixIO__native_handle(descriptor) < 0)
    {
        return -1;
    }
    return 0;
}

int pledge(const char * promises, const char * paths)
{
    return 0;
}
