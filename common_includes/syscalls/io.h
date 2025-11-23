#ifndef _COMMON_INCLUDES_SYSCALLS_IO_H
#define _COMMON_INCLUDES_SYSCALLS_IO_H

typedef enum IOOperation
{
    IO_OPEN,
    IO_CLOSE,
    IO_READ,
    IO_WRITE,
}
IOOperation;

typedef enum IOStatus
{
    IO_OK = 0,
    IO_NO_DEVICE_FOUND = -1,
    IO_REGISTER_FAILED = -2,
    IO_NOT_OPEN = -3,
    IO_UNKNOWN_HANDLE = -4,
    IO_WAIT_FOR_DATA = -5,
    IO_OPERATION_NOT_IMPLEMENTED = -6,
    IO_OPERATION_NOT_SUPPORTED = -7,
    IO_INVALID_MOUNT_POINT = -8,
    IO_INVALID_PATH = -9,
    IO_EOF = -10,
    IO_UNKNOWN_BUFFER = -11,
}
IOStatus;

typedef enum IOFlags
{
    IO_OPEN_READ = 0x1,
    IO_OPEN_WRITE = 0x2,
}
IOFlags;

typedef int64_t IOHandle;
typedef int64_t ByteCount;
typedef uint64_t Flags;

#endif