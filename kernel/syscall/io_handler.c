#include "io_handler.h"
#include "../address.h"
#include "../kernel_core.h"
#include "../arch/syscall.h"
#include <stdint.h>


Result(SystemCallResult) IO__handler(
        KernelPtr kernel, ProcessPtr proc, Anything io_operation, Anything arg2, Anything arg3, Anything arg4, Anything arg5
    )
{
    IOSubsystemPtr io_subsystem = &kernel->io_subsystem;

    IOContext context = {
        .buffer = (DataPtr) arg3,
        .size = (size_t) arg4,
        .proc = proc,
        .handle = (IOHandle) arg2,
        .io_subsystem = io_subsystem
    };

    if (io_operation == IO_OPEN)
    {
        Flags flags = (Flags) arg4;
        KString path = kstring((const char *) arg2);

        ResultMatch(IOHandle, IOSubsystem__open(io_subsystem, proc, path, flags));
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
        ResultMatch(IOHandle, IOSubsystem__close(io_subsystem, proc, (IOHandle) arg2));
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

    KString error_message = kstring("Unhandled io operation: %p");
    KString message = KString__sprintf(&error_message, io_operation);
    return Error(SystemCallResult, message, SYSCALL_FAILURE);
}