#ifndef __KERNEL_SUBSYSTEMS_IO_IO_H
#define __KERNEL_SUBSYSTEMS_IO_IO_H
#include <stdint.h>

#include "types.h"
#include "../../scheduler/kernel_context.h"

#include "../../types/fixed_array.h"
#include "../../types/comparison.h"
#include "../../types/map.h"
#include "../../types/result.h"
#include "../../types/bstring.h"
#include "../../all_defs.h"
#include "context/originating_call_context.h"
#include "subsystems/message/message_broker.h"
#include "syscalls/io.h"

#ifndef __KERNEL_IO_STATUS_RESULT_DEFINED
#define __KERNEL_IO_STATUS_RESULT_DEFINED
ResultDef(IOStatus);
#endif

OptionalDef(IOServerRole);

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
typedef ExternalBuffer * ExternalBufferPtr;
typedef const ExternalBuffer * ConstExternalBufferPtr;

ResultDef(ExternalBuffer);
OptionalDef(ExternalBuffer);
ResultDef(IOServerBuffer);

typedef struct IOContext
{
    // inheritance, keep this in first pos
    KernelContext kernel_context;

    Optional(IOSubsystemPtr) io_subsystem;

    Optional(IOHandle) handle;
    Optional(ExternalBuffer) buffer;
} IOContext;

typedef IOContext * IOContextPtr;

OptionalDef(IOContext);
ResultDef(IOContext);

OptionalDef(IOContextPtr);
ResultDef(IOContextPtr);

typedef Result(IOInternalHandle) (* IO_open)(IOStatePtr self, ConstBStringPtr path, Flags flags);
typedef Result(IOInternalHandle) (* IO_close)(IOStatePtr self, IOInternalHandle handle);
typedef Result(IOBufferPtr) (* IO_read)(
    IOStatePtr self, IOInternalHandle handle, size_t size, ConstExternalBufferPtr target
);
typedef Result(ByteCount) (* IO_write)(IOStatePtr self, IOInternalHandle handle, DataPtr data, size_t size);
typedef Result(IOInternalHandle) (* IO_accept)(IOStatePtr self, IOInternalHandle handle);
typedef Result(ParkStatus)(* IO_park)(IOStatePtr self, IOContextPtr parked_context);
typedef Result(ParkStatus)(* IO_unpark)(IOStatePtr self);
typedef Result(IOServerBuffer) (* IO_server_buffer)(IOStatePtr self);
typedef Result(IOServerBuffer) (* IO_server_write_buffer)(IOStatePtr self);
typedef Optional(IOContextPtr) (* IO_data_available)(IOStatePtr self, size_t byte_count);
typedef Result(IOStatus) (* IO_data_consumed)(IOStatePtr self, size_t byte_count);
typedef Optional(IOContextPtr) (* IO_eof)(IOStatePtr self);

typedef struct IO
{
    IO_open open;
    IO_read read;
    IO_write write;
    IO_accept accept;
    IO_close close;
    IO_park park;
    IO_unpark unpark;
    IO_server_buffer server_buffer;
    IO_server_write_buffer server_write_buffer;
    IO_data_available data_available;
    IO_data_consumed data_consumed;
    IO_eof eof;
} IO;

OptionalDef(IO);
ResultDef(IO);

typedef struct DeviceRegistration
{
    IOPtr device;
    BString path;
    IOStatePtr state;
} DeviceRegistration;

OptionalDef(DeviceRegistration);

typedef DeviceRegistration * DeviceRegistrationPtr;
OptionalDef(DeviceRegistrationPtr);

MapDefine(IOHandle, DeviceRegistrationPtr, 1024, IOHandlesMap, SimpleEquals(uint64_t));
FixedArrayDefine(DeviceRegistration, IO_MAX_DEVICES, IODevicesArray, SimpleEquals(uint64_t));

IOState IOState__new();
IO IO__new(
    IO_open open,
    IO_read read,
    IO_write write,
    IO_accept accept,
    IO_close close,
    IO_park park,
    IO_unpark unpark,
    IO_server_buffer server_buffer,
    IO_server_write_buffer server_write_buffer,
    IO_data_available data_available,
    IO_data_consumed data_consumed,
    IO_eof eof
);
Result(IOInternalHandle) IO__unsupported_open(IOStatePtr self, ConstBStringPtr path, Flags flags);
Result(IOBufferPtr) IO__unsupported_read(
    IOStatePtr self, IOInternalHandle handle, size_t size, ConstExternalBufferPtr target
);
Result(ByteCount) IO__unsupported_write(IOStatePtr self, IOInternalHandle handle, DataPtr data, size_t size);
Result(IOInternalHandle) IO__unsupported_accept(IOStatePtr self, IOInternalHandle handle);
Result(IOInternalHandle) IO__unsupported_close(IOStatePtr self, IOInternalHandle handle);
Result(ParkStatus) IO__unsupported_park(IOStatePtr self, IOContextPtr parked_context);
Result(ParkStatus) IO__unsupported_unpark(IOStatePtr self);
Result(IOServerBuffer) IO__unsupported_server_buffer(IOStatePtr self);
Result(IOServerBuffer) IO__unsupported_server_write_buffer(IOStatePtr self);
Optional(IOContextPtr) IO__unsupported_data_available(IOStatePtr self, size_t byte_count);
Result(IOStatus) IO__unsupported_data_consumed(IOStatePtr self, size_t byte_count);
Optional(IOContextPtr) IO__unsupported_eof(IOStatePtr self);
DeviceRegistration DeviceRegistration__new(IOPtr device, ConstBStringPtr path, IOStatePtr state);

IOContext IOContext__new(
    KernelContextPtr kernel_context, OptionalPtr(IOSubsystemPtr) io_subsystem,
    OptionalPtr(IOHandle) handle, OptionalPtr(ExternalBuffer) buffer
);

IOContextPtr IOContext__heap_new(
    KernelContextPtr kernel_context, OptionalPtr(IOSubsystemPtr) io_subsystem,
    OptionalPtr(IOHandle) handle, OptionalPtr(ExternalBuffer) buffer
);

IOContextPtr IOContext__from_originating_context(
    ConstOriginatingContextPtr originating_context, OptionalPtr(IOHandle) handle,
    OptionalPtr(ExternalBuffer) buffer
);

IOContextPtr IOContext__from_message_listener(KernelListenerPtr listener, ProcessID pid);
IOContextPtr IOContext__from_io_context(IOContextPtr base, OptionalPtr(IOHandle) handle);

void IOContext__dispose(IOContextPtr self);

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
