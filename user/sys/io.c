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

const char * io_status_string(IOStatus status)
{
    switch (status)
    {
        case IO_OK:
            return "IO_OK";
        case IO_NO_DEVICE_FOUND:
            return "IO_NO_DEVICE_FOUND";
        case IO_REGISTER_FAILED:
            return "IO_REGISTER_FAILED";
        case IO_NOT_OPEN:
            return "IO_NOT_OPEN";
        case IO_UNKNOWN_HANDLE:
            return "IO_UNKNOWN_HANDLE";
        case IO_WAIT_FOR_DATA:
            return "IO_WAIT_FOR_DATA";
        case IO_OPERATION_NOT_IMPLEMENTED:
            return "IO_OPERATION_NOT_IMPLEMENTED";
        case IO_OPERATION_NOT_SUPPORTED:
            return "IO_OPERATION_NOT_SUPPORTED";
        case IO_INVALID_MOUNT_POINT:
            return "IO_INVALID_MOUNT_POINT";
        case IO_INVALID_PATH:
            return "IO_INVALID_PATH";
        case IO_EOF:
            return "IO_EOF";
        case IO_UNKNOWN_BUFFER:
            return "IO_UNKNOWN_BUFFER";
        case IO_UNKNOWN_PID:
            return "IO_UNKNOWN_PID";
        case IO_PANIC:
            return "IO_PANIC";
    }

    return "IO_UNKNOWN_ERROR";
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
