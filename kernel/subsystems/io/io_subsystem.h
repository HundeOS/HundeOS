#ifndef __KERNEL_IO_SUBSYSTEM_H
#define __KERNEL_IO_SUBSYSTEM_H

#include <stdint.h>

#include "../../types/result.h"
#include "../../types/optional.h"
#include "../../address.h"
#include "../../process.h"

#include "io.h"

typedef uint64_t DeviceHandle;

typedef struct IOSubsystem
{
    IODevicesArray devices;
    IOHandlesMap handles;
} IOSubsystem;

typedef IOSubsystem * IOSubsystemPtr;

ResultDef(IOSubsystem);
ResultDef(IOSubsystemPtr);

Result(DeviceHandle) IOSubsystem__register_device(IOSubsystemPtr self, KString path, IOPtr device, IOStatePtr device_state);

Result(IOHandle) IOSubsystem__open(IOSubsystemPtr self, ProcessPtr proc, KString path, Flags flags);
Result(IOHandle) IOSubsystem__close(IOSubsystemPtr self, ProcessPtr proc, IOHandle device);
Result(ByteCount) IOSubsystem__read(IOSubsystemPtr self, ProcessPtr proc, IOHandle device, DataPtr buffer, size_t size);
Result(ByteCount) IOSubsystem__write(IOSubsystemPtr self, ProcessPtr proc, IOHandle device, DataPtr data, size_t size);

// called by IRQs
Result(IOHandle) IOSubsystem__async_receive(IOSubsystemPtr self, KString path, Flags flags);

#define NEW_IO_SUBSYSTEM \
    (IOSubsystem) { \
        .devices = EmptyArray(IODevicesArray), \
        .handles = EmptyMap(IOHandlesMap), \
        \
    }

#define NEW_IO_STATE \
    (IOState) { \
        .handles = EmptyArray(IOHandleArray) \
    }

#endif