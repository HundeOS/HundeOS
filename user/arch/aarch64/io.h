#ifndef __USER_IO_H
#define __USER_IO_H

#include <stddef.h>
#include <stdint.h>

typedef enum IOOperation
{
    IO_OPEN,
    IO_CLOSE,
    IO_READ,
    IO_WRITE,
} IOOperation;

typedef enum IOStatus
{
    IO_OK = 0,
    IO_ERROR = -1,
} IOStatus;

typedef int64_t IOHandle;
typedef int64_t ByteCount;
typedef uint64_t Flags;

IOHandle open(const char * path, Flags flags);
ByteCount read(IOHandle handle, char * buf, size_t size);
ByteCount write(IOHandle handle, char * buf, size_t size);
IOStatus close(IOHandle handle);

#endif