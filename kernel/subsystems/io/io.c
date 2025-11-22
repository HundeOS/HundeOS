#include "io.h"
#include "../../kernel_core.h"

FixedArrayBodyDefine(IODevicesArray);
MapBodyDefine(IOHandlesMap);

IOContext IOContext__new(
        KernelContext kernel_context, IOSubsystemPtr io_subsystem, Optional(ProcessPtr) proc, IOHandle handle, DataPtr buffer, size_t size
    )
{
    return (IOContext) {
        .kernel_context = kernel_context,
        .io_subsystem = io_subsystem,
        .proc = proc,
        .handle = handle,
        .buffer = buffer,
        .size = size
    };
}

IOContext IOContext__from_originating_context(
    OriginatingContext originating_context, IOHandle handle, DataPtr buffer, size_t size
)
{
    KernelPtr kernel = originating_context.kernel;
    KernelContext kernel_context = KernelContext__from_originating_context(originating_context);
    return IOContext__new(kernel_context, &kernel->io_subsystem, originating_context.process, handle, buffer, size);
}
