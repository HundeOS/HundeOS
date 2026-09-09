#include "io.h"
#include "../../kernel_core.h"
#include "../../kalloc.h"

FixedArrayBodyDefine(IODevicesArray);
MapBodyDefine(IOHandlesMap);

static BString io_operation_not_supported = bstr("IO operation not supported");

IOState IOState__new()
{
    return (IOState){
        .handles = EmptyArray(IOHandleArray),
    };
}

IO IO__new(IO_open open, IO_read read, IO_write write, IO_accept accept, IO_close close, IO_park park, IO_unpark unpark,
           IO_server_buffer server_buffer, IO_server_write_buffer server_write_buffer, IO_data_available data_available,
           IO_data_consumed data_consumed, IO_eof eof)
{
    return (IO){
        .open = open,
        .read = read,
        .write = write,
        .accept = accept,
        .close = close,
        .park = park,
        .unpark = unpark,
        .server_buffer = server_buffer,
        .server_write_buffer = server_write_buffer,
        .data_available = data_available,
        .data_consumed = data_consumed,
        .eof = eof,
    };
}

Result(IOInternalHandle) IO__unsupported_open(IOStatePtr self, ConstBStringPtr path, Flags flags)
{
    return Error(IOInternalHandle, io_operation_not_supported, IO_OPERATION_NOT_SUPPORTED);
}

Result(IOBufferPtr)
    IO__unsupported_read(IOStatePtr self, IOInternalHandle handle, size_t size, ConstExternalBufferPtr target)
{
    return Error(IOBufferPtr, io_operation_not_supported, IO_OPERATION_NOT_SUPPORTED);
}

Result(ByteCount) IO__unsupported_write(IOStatePtr self, IOInternalHandle handle, DataPtr data, size_t size)
{
    return Error(ByteCount, io_operation_not_supported, IO_OPERATION_NOT_SUPPORTED);
}

Result(IOInternalHandle) IO__unsupported_accept(IOStatePtr self, IOInternalHandle handle)
{
    return Error(IOInternalHandle, io_operation_not_supported, IO_OPERATION_NOT_SUPPORTED);
}

Result(IOInternalHandle) IO__unsupported_close(IOStatePtr self, IOInternalHandle handle)
{
    return Error(IOInternalHandle, io_operation_not_supported, IO_OPERATION_NOT_SUPPORTED);
}

Result(ParkStatus) IO__unsupported_park(IOStatePtr self, IOContextPtr parked_context)
{
    return Error(ParkStatus, io_operation_not_supported, IO_OPERATION_NOT_SUPPORTED);
}

Result(ParkStatus) IO__unsupported_unpark(IOStatePtr self)
{
    return Error(ParkStatus, io_operation_not_supported, IO_OPERATION_NOT_SUPPORTED);
}

Result(IOServerBuffer) IO__unsupported_server_buffer(IOStatePtr self)
{
    return Error(IOServerBuffer, io_operation_not_supported, IO_OPERATION_NOT_SUPPORTED);
}

Result(IOServerBuffer) IO__unsupported_server_write_buffer(IOStatePtr self)
{
    return Error(IOServerBuffer, io_operation_not_supported, IO_OPERATION_NOT_SUPPORTED);
}

Optional(IOContextPtr) IO__unsupported_data_available(IOStatePtr self, size_t byte_count)
{
    return None(IOContextPtr);
}

Result(IOStatus) IO__unsupported_data_consumed(IOStatePtr self, size_t byte_count)
{
    return Error(IOStatus, io_operation_not_supported, IO_OPERATION_NOT_SUPPORTED);
}

Optional(IOContextPtr) IO__unsupported_eof(IOStatePtr self)
{
    return None(IOContextPtr);
}

DeviceRegistration DeviceRegistration__new(IOPtr device, ConstBStringPtr path, IOStatePtr state)
{
    return (DeviceRegistration){
        .device = device,
        .path = *path,
        .state = state,
    };
}

IOContext IOContext__new(KernelContextPtr kernel_context, OptionalPtr(IOSubsystemPtr) io_subsystem,
                         OptionalPtr(IOHandle) handle, OptionalPtr(ExternalBuffer) buffer)
{
    return (IOContext){
        .kernel_context = *kernel_context,
        .io_subsystem = *io_subsystem,
        .handle = *handle,
        .buffer = *buffer,
    };
}

IOContextPtr IOContext__heap_new(KernelContextPtr kernel_context, OptionalPtr(IOSubsystemPtr) io_subsystem,
                                 OptionalPtr(IOHandle) handle, OptionalPtr(ExternalBuffer) buffer)
{
    IOContextPtr context = ptr(Kernel__alloc_kernel(sizeof(IOContext)), IOContext);
    *context = IOContext__new(kernel_context, io_subsystem, handle, buffer);

    return context;
}

IOContextPtr IOContext__from_originating_context(ConstOriginatingContextPtr originating_context,
                                                 OptionalPtr(IOHandle) handle, OptionalPtr(ExternalBuffer) buffer)
{
    KernelPtr kernel = originating_context->kernel;
    KernelContext kernel_context = KernelContext__from_originating_context(originating_context);
    return IOContext__heap_new(&kernel_context, &Some(IOSubsystemPtr, kernel->io_subsystem), handle, buffer);
}

IOContextPtr IOContext__from_message_listener(KernelListenerPtr listener, ProcessID pid)
{
    KernelPtr kernel = listener->kernel;

    KernelContext kernel_context = KernelContext__new(&Some(ProcessID, pid), &Some(SchedulerPtr, kernel->scheduler));

    return IOContext__heap_new(&kernel_context, &Some(IOSubsystemPtr, kernel->io_subsystem), &None(IOHandle),
                               &None(ExternalBuffer));
}

IOContextPtr IOContext__from_io_context(IOContextPtr self, OptionalPtr(IOHandle) handle)
{
    return IOContext__heap_new(&self->kernel_context, &self->io_subsystem, handle, &self->buffer);
}

void IOContext__dispose(IOContextPtr self)
{
    Kernel__free(addr(self));
}

ExternalBuffer ExternalBuffer__new(DataPtr data, size_t len)
{
    return (ExternalBuffer){
        .data = data,
        .len = len,
    };
}
