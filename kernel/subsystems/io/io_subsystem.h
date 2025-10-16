#ifndef __KERNEL_IO_SUBSYSTEM_H
#define __KERNEL_IO_SUBSYSTEM_H

#include <stdint.h>

#include "../../types/result.h"
#include "../../types/optional.h"

#include "io.h"

typedef uint64_t DeviceHandle;

OptionalDef(IO);

ResultDef(DeviceHandle);
ResultDef(IOHandle);

Result(DeviceHandle) IOSubsystem__register_device(Optional(IO) device);
Result(IOHandle) IOSubsystem__open(KString path, Flags flags);
Result(BufferPtr) IOSubsystem__read(Optional(IO) device);


#endif