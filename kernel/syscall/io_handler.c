#include "io_handler.h"
#include "../address.h"
#include "../kernel_core.h"
#include "../arch/syscall.h"
#include <stdint.h>


static KString unhandled_operation_format = KSTRING_NEW("Unhandled io operation: %p");

#define CompleteCall(RT, context, block) \
    Result(RT) res = block; \
    IOContext__dispose(context); \
    ResultMatch(RT, res);\
        IsError(code, message, \
        { \
            return Error(SystemCallResult, message, code); \
        }); \
        IsSuccess(val, \
        { \
            return Success(SystemCallResult, val); \
        }) \
    End \


Result(SystemCallResult) IO__handler(
    KernelPtr kernel, Optional(ProcessPtr) proc, Anything io_operation,
    Anything arg2, Anything arg3, Anything arg4, Anything arg5
)
{
    IOSubsystemPtr io_subsystem = kernel->io_subsystem;
    Optional(ProcessID) pid = None(ProcessID);
    Match(ProcessPtr, proc)
        IsSome(process, pid = Some(ProcessID, process->pid));
    End

    KernelContext kernel_context = KernelContext__new(proc, pid, Some(SchedulerPtr, kernel->scheduler));
    IOContextPtr context = NULL;

    if (io_operation == IO_OPEN)
    {
        Flags flags = (Flags) arg4;
        KString path = kstring((const char *) arg2);
        context =
            IOContext__heap_new(
                kernel_context, Some(IOSubsystemPtr, io_subsystem), None(IOHandle), None(ExternalBuffer)
            );

        CompleteCall(IOHandle, context, IOSubsystem__open(io_subsystem, context, path, flags));
    }

    if (io_operation == IO_CLOSE)
    {
        context = IOContext__heap_new(
            kernel_context, Some(IOSubsystemPtr, io_subsystem), Some(IOHandle, (IOHandle) arg2), None(ExternalBuffer)
        );

        CompleteCall(IOHandle, context, IOSubsystem__close(io_subsystem, context));
    }

    if (io_operation == IO_READ)
    {
        ExternalBuffer buffer = ExternalBuffer__new((DataPtr) arg3, (size_t) arg4);

        context =
            IOContext__heap_new(
                kernel_context, Some(IOSubsystemPtr, io_subsystem),
                Some(IOHandle, (IOHandle) arg2), Some(ExternalBuffer, buffer)
            );

        CompleteCall(ByteCount, context, IOSubsystem__read(io_subsystem, context));
    }

    if (io_operation == IO_WRITE)
    {
        ExternalBuffer buffer = ExternalBuffer__new((DataPtr) arg3, (size_t) arg4);
        context =
            IOContext__heap_new(
                kernel_context, Some(IOSubsystemPtr, io_subsystem),
                Some(IOHandle, (IOHandle) arg2), Some(ExternalBuffer, buffer)
            );

        CompleteCall(ByteCount, context, IOSubsystem__write(io_subsystem, context));
    }

    return Error(SystemCallResult, KString__sprintf(&unhandled_operation_format, io_operation), SYSCALL_FAILURE);
}