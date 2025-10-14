#ifndef __KERNEL_SUBSYSTEMS_IO_IO_H
#define __KERNEL_SUBSYSTEMS_IO_IO_H
#include <stdint.h>
#include "../../address.h"
#include "../../types/optional.h"

typedef uint64_t IOHandle;
typedef uint64_t Flags;

OptionalDef(IOHandle, OptionalIOHandle);
OptionalDef(KernelAddress, OptionalKernelAddress);

OptionalIOHandle (* IO__open)(const char *path, Flags flags);
OptionalKernelAddress (* IO__read)(IOHandle handle, size_t size);
Result (* IO__write)(IOHandle handle, size_t size);

typedef struct IO
{
    IO__open open;
    IO__read read;
    IO__write write;
} IO;

#endif