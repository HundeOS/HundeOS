#ifndef __KERNEL_SUBSYSTEMS_IO_IO_H
#define __KERNEL_SUBSYSTEMS_IO_IO_H
#include <stdint.h>

#include "../../types/ring_buffer.h"

#include "../../types/fixed_array.h"
#include "../../types/comparison.h"
#include "../../types/map.h"
#include "../../types/result.h"
#include "../../types/kstring.h"
#include "../../all_defs.h"


#define _4_MEGS 4194304

typedef uint8_t Data;
typedef Data * DataPtr;

typedef uint64_t IOHandle;
typedef uint64_t DeviceHandle;
typedef uint64_t Flags;
typedef size_t ByteCount;

typedef struct IO IO;
typedef IO * IOPtr;

ResultDef(IOHandle);
ResultDef(DeviceHandle);
ResultDef(ByteCount);

OptionalDef(IOHandle);
OptionalDef(DeviceHandle);
OptionalDef(IOPtr);

FixedArrayDefine(IOHandle, MAX_HANDLES_PER_DEVICE, IOHandleArray);
RingBufferDefine(_4_MEGS, IOBuffer);

typedef IOBuffer * IOBufferPtr;
ResultDef(IOBufferPtr);

typedef struct IOState
{
    IOHandleArray handles;
} IOState;

typedef IOState * IOStatePtr;


typedef Result(IOHandle) (* IO_open)(IOStatePtr state, KString path, Flags flags);
typedef Result(IOHandle) (* IO_close)(IOStatePtr state, IOHandle handle);
typedef Result(IOBufferPtr) (* IO_read)(IOStatePtr state, IOHandle handle, size_t size);
typedef Result(ByteCount) (* IO_write)(IOStatePtr state, IOHandle handle, DataPtr data, size_t size);

typedef struct IO
{
    IO_open open;
    IO_read read;
    IO_write write;
    IO_close close;
} IO;

OptionalDef(IO);
ResultDef(IO);

typedef enum IOStatus
{
    IO_OK = 0,
    IO_NO_DEVICE_FOUND = -1,
    IO_REGISTER_FAILED = -2,
    IO_NOT_OPEN = -3,
    IO_UNKNOWN_HANDLE = -4,
} IOStatus;

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
FixedArrayDefine(DeviceRegistration, IO_MAX_DEVICES, IODevicesArray);

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