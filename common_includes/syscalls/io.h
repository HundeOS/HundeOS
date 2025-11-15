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
}
IOStatus;

typedef int64_t IOHandle;
typedef int64_t ByteCount;
typedef uint64_t Flags;

#endif