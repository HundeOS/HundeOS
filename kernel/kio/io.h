#ifndef __KERNEL_KIO_IO_H
#define __KERNEL_KIO_IO_H
#include "../subsystems/io/io.h"
#include "../kernel_core.h"
#include "../types/fixed_array.h"
#include "../context/originating_call_context.h"

FixedArrayDefine(Char, _16MB, ReadBuffer, SimpleEquals(Char));
typedef ReadBuffer * ReadBufferPtr;

Result(IOHandle) open(OriginatingContext context, KString path, Flags flags);
Result(ByteCount) read(OriginatingContext context, ReadBufferPtr buffer);

#endif