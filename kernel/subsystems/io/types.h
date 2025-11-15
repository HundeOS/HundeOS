#ifndef __KERNEL_SUBSYSTEMS_IO_TYPES_H
#define __KERNEL_SUBSYSTEMS_IO_TYPES_H

#include <stdint.h>

#include "../../types/fixed_array.h"
#include "../../types/ring_buffer.h"
#include "../../types/result.h"
#include "../../types/optional.h"
#include "syscalls/io.h"

#define _4_MEGS 4194304

typedef uint8_t Data;
typedef Data * DataPtr;

typedef uint32_t IOInternalHandle;
typedef uint32_t DeviceHandle;

ResultDef(IOHandle);
ResultDef(IOInternalHandle);
ResultDef(DeviceHandle);
ResultDef(ByteCount);

OptionalDef(IOHandle);
OptionalDef(IOInternalHandle);
OptionalDef(DeviceHandle);

FixedArrayDefine(IOHandle, MAX_HANDLES_PER_DEVICE, IOHandleArray, SimpleEquals(uint64_t));
RingBufferDefine(_4_MEGS, IOBuffer);

typedef IOBuffer * IOBufferPtr;

ResultDef(IOBufferPtr);


#endif