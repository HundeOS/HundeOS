#include "io.h"

#include <stddef.h>

#include "syscall.h"

IOHandle open(const char *path, Flags flags)
{
    return syscall(IO_CALL, IO_OPEN, any(path), flags, 0, 0);
}

ByteCount read(IOHandle handle, char *buf, size_t size)
{
    return syscall(IO_CALL, IO_READ, handle, any(buf), size, 0);
}

ByteCount write(IOHandle handle, char *buf, size_t size)
{
    return syscall(IO_CALL, IO_WRITE, handle, any(buf), size, 0);
}

IOStatus close(IOHandle handle)
{
    IOStatus result = (IOStatus) syscall(IO_CALL, IO_CLOSE, handle, 0, 0, 0);
    if (result > 0)
    {
        return IO_OK;
    }

    return result;
}

DirectoryEntryCount list_dir(const char * path, DirectoryEntryPtr entries, size_t max_entries)
{
    return syscall(IO_CALL, IO_LIST_DIR, any(path), any(entries), max_entries, 0);
}

IOStatus make_dir(const char * path)
{
    return syscall(IO_CALL, IO_MKDIR, any(path), 0, 0, 0);
}

IOStatus remove_file(const char * path)
{
    return remove_file_with_flags(path, 0);
}

IOStatus remove_file_with_flags(const char * path, Flags flags)
{
    return syscall(IO_CALL, IO_REMOVE, any(path), flags, 0, 0);
}
