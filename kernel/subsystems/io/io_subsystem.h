#ifndef __KERNEL_IO_SUBSYSTEM_H
#define __KERNEL_IO_SUBSYSTEM_H

#include <stdint.h>

#include "../../types/result.h"
#include "../../types/optional.h"
#include "../../address.h"
#include "../../process.h"
#include "../../types/comparison.h"

#include "io.h"

typedef struct IOContext IOContext;
typedef struct Scheduler Scheduler;
typedef Scheduler * SchedulerPtr;

typedef IOContext * IOContextPtr;

typedef struct IOSubsystem
{
    IODevicesArray devices;
    IOHandlesMap handles;

    SchedulerPtr scheduler;
} IOSubsystem;

typedef IOSubsystem * IOSubsystemPtr;

ResultDef(IOSubsystem);
ResultDef(IOSubsystemPtr);

Result(DeviceHandle) IOSubsystem__register_device(IOSubsystemPtr self, KString path, IOPtr device, IOStatePtr device_state);

Result(IOHandle) IOSubsystem__open(IOSubsystemPtr self, ProcessPtr proc, KString path, Flags flags);
Result(IOHandle) IOSubsystem__close(IOSubsystemPtr self, ProcessPtr proc, IOHandle device);
Result(ByteCount) IOSubsystem__read(IOSubsystemPtr self, IOContext context);
Result(ByteCount) IOSubsystem__write(IOSubsystemPtr self, IOContext context);

void IOSubsystem__data_arrived(IOSubsystemPtr self, IOContext parked_context);

// called by IRQs
Result(IOHandle) IOSubsystem__async_receive(IOSubsystemPtr self, KString path, Flags flags);

#define NEW_IO_SUBSYSTEM(sched) \
    (IOSubsystem) { \
        .devices = EmptyArray(IODevicesArray), \
        .handles = EmptyMap(IOHandlesMap), \
        .scheduler = sched, \
        \
    }

#define NEW_IO_STATE \
    (IOState) { \
        .handles = EmptyArray(IOHandleArray) \
    }

#endif