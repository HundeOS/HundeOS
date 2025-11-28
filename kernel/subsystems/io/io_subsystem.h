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


FixedArrayDefine(IOHandle, MAX_HANDLES, PIDHandles, SimpleEquals(uint64_t));
typedef PIDHandles * PIDHandlesPtr;
OptionalDef(PIDHandles);
OptionalDef(PIDHandlesPtr);

ResultDef(PIDHandles);
ResultDef(PIDHandlesPtr);

MapDefine(ProcessID, PIDHandles, MAX_PROCESSES, ProcessHandleMap, SimpleEquals(uint64_t));
typedef ProcessHandleMap * ProcessHandleMapPtr;

typedef struct IOSubsystem
{
    IODevicesArray devices;
    IOHandlesMap handles;

    SchedulerPtr scheduler;
    ProcessHandleMap process_handles;
} IOSubsystem;

typedef IOSubsystem * IOSubsystemPtr;

ResultDef(IOSubsystem);
ResultDef(IOSubsystemPtr);

Result(DeviceHandle) IOSubsystem__register_device(IOSubsystemPtr self, KString path, IOPtr device, IOStatePtr device_state);

Result(IOHandle) IOSubsystem__open(IOSubsystemPtr self, IOContext context, KString path, Flags flags);
Result(IOHandle) IOSubsystem__close(IOSubsystemPtr self, IOContext context);
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
        .process_handles = EmptyMap(ProcessHandleMap),\
        \
    }

#define IO_SUBSYSTEM_INIT(subsystem, sched) \
        ArrayInit(IODevicesArray, subsystem.devices); \
        MapInit(IOHandlesMap, subsystem.handles); \
        subsystem.scheduler = sched; \
        MapInit(ProcessHandleMap, subsystem.process_handles);\

#define NEW_IO_STATE \
    (IOState) { \
        .handles = EmptyArray(IOHandleArray) \
    }

#endif