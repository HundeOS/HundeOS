#include "io_handler.h"
#include "../address.h"
#include "../kernel_core.h"
#include "../arch/syscall.h"
#include <stdint.h>


static KString unhandled_operation_format = KSTRING_NEW("Unhandled io operation: %p");

Result(SystemCallResult) IO__handler(
    KernelPtr kernel, Optional(ProcessPtr) proc, Anything io_operation,
    Anything arg2, Anything arg3, Anything arg4, Anything arg5
)
{
    IOSubsystemPtr io_subsystem = &kernel->io_subsystem;
    KernelContext kernel_context = KernelContext__new(proc, kernel->scheduler);

    if (io_operation == IO_OPEN)
    {
        Flags flags = (Flags) arg4;
        KString path = kstring((const char *) arg2);
        IOContext context = IOContext__new(kernel_context, io_subsystem, None(IOHandle), None(ExternalBuffer));

        ResultMatch(IOHandle, IOSubsystem__open(io_subsystem, context, path, flags));
            IsError(code, message,
            {
                return Error(SystemCallResult, message, code);
            });
            IsSuccess(handle,
            {
                return Success(SystemCallResult, handle);
            })
        End
    }

    if (io_operation == IO_CLOSE)
    {
        IOContext context = IOContext__new(
            kernel_context, io_subsystem, Some(IOHandle, (IOHandle) arg2), None(ExternalBuffer)
        );

        ResultMatch(IOHandle, IOSubsystem__close(io_subsystem, context));
            IsError(code, message,
            {
                return Error(SystemCallResult, message, code);
            });
            IsSuccess(handle,
            {
                return Success(SystemCallResult, handle);
            })
        End
    }

    if (io_operation == IO_READ)
    {
        ExternalBuffer buffer = ExternalBuffer__new((DataPtr) arg3, (size_t) arg4);

        IOContext context = IOContext__new(
            kernel_context, io_subsystem, Some(IOHandle, (IOHandle) arg2), Some(ExternalBuffer, buffer)
        );

        ResultMatch(ByteCount, IOSubsystem__read(io_subsystem, context));
            IsError(code, message,
            {
                return Error(SystemCallResult, message, code);
            });
            IsSuccess(bytes_read,
            {
                return Success(SystemCallResult, bytes_read);
            })
        End
    }

    if (io_operation == IO_WRITE)
    {
        ExternalBuffer buffer = ExternalBuffer__new((DataPtr) arg3, (size_t) arg4);
        IOContext context = IOContext__new(
            kernel_context, io_subsystem, Some(IOHandle, (IOHandle) arg2), Some(ExternalBuffer, buffer)
        );

        ResultMatch(ByteCount, IOSubsystem__write(io_subsystem, context));
            IsError(code, message,
            {
                return Error(SystemCallResult, message, code);
            });
            IsSuccess(bytes,
            {
                return Success(SystemCallResult, bytes);
            })
        End
    }

    return Error(SystemCallResult, KString__sprintf(&unhandled_operation_format, io_operation), SYSCALL_FAILURE);
}