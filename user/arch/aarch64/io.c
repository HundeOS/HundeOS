#include "../../sys/io.h"

#include <stddef.h>

#include "../syscall.h"

IOHandle open(const char *path, Flags flags)
{
    return syscall(IO_CALL, IO_OPEN, any(path), flags, 0, 0);
}

ByteCount read(IOHandle handle, char *buf, size_t size)
{
    return syscall(IO_CALL, IO_READ, handle, any(buf), size, 0);
}
