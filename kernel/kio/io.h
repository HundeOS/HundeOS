#ifndef __KERNEL_KIO_IO_H
#define __KERNEL_KIO_IO_H
#include "../subsystems/io/io.h"
#include "../kernel_core.h"
#include "../types/fixed_array.h"
#include "../context/originating_call_context.h"

FixedArrayDefine(Char, _16MB, DataBuffer, SimpleEquals(Char));
typedef DataBuffer * DataBufferPtr;

ExternalBuffer DataBuffer__convert_to_external_buffer(DataBufferPtr self);
DataPtr DataBuffer__data(DataBufferPtr self);
void DataBuffer__dispose(DataBufferPtr self);

Result(IOHandle) open(ConstOriginatingContextPtr context, ConstBStringPtr path, Flags flags);
Result(ByteCount) read(ConstOriginatingContextPtr context, IOHandle handle, DataBufferPtr buffer);
Result(ByteCount) read_into(ConstOriginatingContextPtr context, IOHandle handle, DataPtr target, size_t size);
Result(ByteCount) write(ConstOriginatingContextPtr context, IOHandle handle, DataBufferPtr buffer);
Result(IOHandle) close(ConstOriginatingContextPtr context, IOHandle handle);

#define open(context, path, flags) open(&(context), &(path), (flags))

#define read(context, handle, buffer) read(&(context), (handle), (buffer))

#define read_into(context, handle, target, size) read_into(&(context), (handle), (target), (size))

#define write(context, handle, buffer) write(&(context), (handle), (buffer))

#define close(context, handle) close(&(context), (handle))

#endif
