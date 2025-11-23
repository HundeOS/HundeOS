#ifndef __KERNEL_KIO_IO_H
#define __KERNEL_KIO_IO_H
#include "../subsystems/io/io.h"
#include "../kernel_core.h"
#include "../types/fixed_array.h"
#include "../context/originating_call_context.h"

FixedArrayDefine(Char, _16MB, DataBuffer, SimpleEquals(Char));
typedef DataBuffer * DataBufferPtr;


ExternalBuffer DataBuffer__convert_to_external_buffer(DataBufferPtr self);


Result(IOHandle) open(OriginatingContext context, KString path, Flags flags);
Result(ByteCount) read(OriginatingContext context, IOHandle handle, DataBufferPtr buffer);
Result(ByteCount) write(OriginatingContext context, IOHandle handle, DataBufferPtr buffer);
Result(IOHandle) close(OriginatingContext context, IOHandle handle);

#endif