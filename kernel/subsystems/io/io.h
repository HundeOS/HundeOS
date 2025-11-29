#ifndef __KERNEL_SUBSYSTEMS_IO_IO_H
#define __KERNEL_SUBSYSTEMS_IO_IO_H
#include <stdint.h>

#include "types.h"
#include "../../scheduler/kernel_context.h"

#include "../../types/fixed_array.h"
#include "../../types/comparison.h"
#include "../../types/map.h"
#include "../../types/result.h"
#include "../../types/kstring.h"
#include "../../all_defs.h"
#include "context/originating_call_context.h"
#include "subsystems/message/message_broker.h"
#include "syscalls/io.h"


typedef struct IO IO;
typedef IO * IOPtr;

typedef struct IOSubsystem IOSubsystem;
typedef IOSubsystem * IOSubsystemPtr;

OptionalDef(IOPtr);
OptionalDef(IOSubsystemPtr);

typedef struct IOState
{
    IOHandleArray handles;
}
IOState;

typedef IOState * IOStatePtr;

typedef enum ParkStatus
{
    IO_PARK_OK = 0,
    IO_UNPARK_OK = 1,
}
ParkStatus;

ResultDef(ParkStatus);

typedef struct ExternalBuffer
{
    DataPtr data;
    size_t len;
} ExternalBuffer;

ResultDef(ExternalBuffer);
OptionalDef(ExternalBuffer);

typedef struct IOContext
{
    // inheritance, keep this in first pos
    KernelContext kernel_context;

    Optional(IOSubsystemPtr) io_subsystem;

    Optional(IOHandle) handle;
    Optional(ExternalBuffer) buffer;
} IOContext;

OptionalDef(IOContext);
ResultDef(IOContext);

typedef Result(IOInternalHandle) (* IO_open)(IOStatePtr state, KString path, Flags flags);
typedef Result(IOInternalHandle) (* IO_close)(IOStatePtr state, IOInternalHandle handle);
typedef Result(IOBufferPtr) (* IO_read)(IOStatePtr state, IOInternalHandle handle, size_t size);
typedef Result(ByteCount) (* IO_write)(IOStatePtr state, IOInternalHandle handle, DataPtr data, size_t size);
typedef Result(ParkStatus)(* IO_park)(IOStatePtr state, IOContext parked_context);
typedef Result(ParkStatus)(* IO_unpark)(IOStatePtr state);

typedef struct IO
{
    IO_open open;
    IO_read read;
    IO_write write;
    IO_close close;
    IO_park park;
    IO_unpark unpark;
} IO;

OptionalDef(IO);
ResultDef(IO);

typedef struct DeviceRegistration
{
    IOPtr device;
    KString path;
    IOStatePtr state;
} DeviceRegistration;

OptionalDef(DeviceRegistration);

typedef DeviceRegistration * DeviceRegistrationPtr;
OptionalDef(DeviceRegistrationPtr);

MapDefine(IOHandle, DeviceRegistrationPtr, 1024, IOHandlesMap, SimpleEquals(uint64_t));
FixedArrayDefine(DeviceRegistration, IO_MAX_DEVICES, IODevicesArray, SimpleEquals(uint64_t));

IOContext IOContext__new(
    KernelContext kernel_context, Optional(IOSubsystemPtr) io_subsystem,
    Optional(IOHandle) handle, Optional(ExternalBuffer) buffer
);

IOContext IOContext__from_originating_context(
    OriginatingContext originating_context, Optional(IOHandle) handle,
    Optional(ExternalBuffer) buffer
);

IOContext IOContext__from_message_listener(KernelListenerPtr listener, ProcessID pid);
IOContext IOContext__from_io_context(IOContext base, Optional(IOHandle) handle);

ExternalBuffer ExternalBuffer__new(DataPtr data, size_t len);

#define NEW_BUFFER \
    (Buffer) { \
        .len = 0, \
        .max = _4_MEGS, \
    } \

#define NEW_IO_STATE \
    (IOState) {\
        .handles = EmptyArray(IOHandleArray),\
    } \

#endif