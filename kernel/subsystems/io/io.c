#include "io.h"
#include "../../kernel_core.h"

FixedArrayBodyDefine(IODevicesArray);
MapBodyDefine(IOHandlesMap);


IOContext IOContext__new(
    KernelContext kernel_context, Optional(IOSubsystemPtr) io_subsystem,
    Optional(IOHandle) handle, Optional(ExternalBuffer) buffer
)
{
    return (IOContext) {
        .kernel_context = kernel_context,
        .io_subsystem = io_subsystem,
        .handle = handle,
        .buffer = buffer,
    };
}

IOContextPtr IOContext__heap_new(
    KernelContext kernel_context, Optional(IOSubsystemPtr) io_subsystem,
    Optional(IOHandle) handle, Optional(ExternalBuffer) buffer
)
{
    IOContextPtr context = ptr(Kernel__alloc_kernel(sizeof(IOContext)), IOContext);
    *context = IOContext__new(kernel_context, io_subsystem, handle, buffer);

    return context;
}

IOContextPtr IOContext__from_originating_context(
    OriginatingContext originating_context,
    Optional(IOHandle) handle, Optional(ExternalBuffer) buffer
)
{
    KernelPtr kernel = originating_context.kernel;
    KernelContext kernel_context = KernelContext__from_originating_context(originating_context);
    return IOContext__heap_new(kernel_context, Some(IOSubsystemPtr, kernel->io_subsystem), handle, buffer);
}

IOContextPtr IOContext__from_message_listener(KernelListenerPtr listener, ProcessID pid)
{
    KernelPtr kernel = listener->kernel;

    KernelContext kernel_context =
        KernelContext__new(None(ProcessPtr), Some(ProcessID, pid), Some(SchedulerPtr, kernel->scheduler));

    return IOContext__heap_new(
        kernel_context, Some(IOSubsystemPtr, kernel->io_subsystem), None(IOHandle), None(ExternalBuffer)
    );
}

IOContextPtr IOContext__from_io_context(IOContextPtr self, Optional(IOHandle) handle)
{
    return IOContext__heap_new(self->kernel_context, self->io_subsystem, handle, self->buffer);
}

void IOContext__dispose(IOContextPtr self)
{
    // TODO: Implement free!
}

ExternalBuffer ExternalBuffer__new(DataPtr data, size_t len)
{
    return (ExternalBuffer) {
        .data = data,
        .len = len,
    };
}
