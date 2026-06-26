#include "io_handler.h"
#include "../address.h"
#include "../kernel_core.h"
#include "../arch/syscall.h"
#include <stdint.h>


static KString unhandled_operation_format = KSTRING_NEW("Unhandled io operation: %p");
static KString no_process = KSTRING_NEW("No process for IO syscall");

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
    ProcessPtr process = NULL;
    Match(ProcessPtr, proc)
        IsSome(current_process,
        {
            process = current_process;
            pid = Some(ProcessID, process->pid);
        });
    End

    if (process == NULL)
    {
        return Error(SystemCallResult, no_process, IO_UNKNOWN_PID);
    }

    KernelContext kernel_context = KernelContext__new(pid, Some(SchedulerPtr, kernel->scheduler));
    IOContextPtr context = NULL;

    if (io_operation == IO_OPEN)
    {
        Flags flags = (Flags) arg3;
        KString path = kstring((const char *) arg2);
        context =
            IOContext__heap_new(
                kernel_context, Some(IOSubsystemPtr, io_subsystem), None(IOHandle), None(ExternalBuffer)
            );

        Result(IOHandle) open_result = IOSubsystem__open(io_subsystem, context, path, flags);
        IOContext__dispose(context);

        ResultMatch(IOHandle, open_result)
            IsError(code, message, return Error(SystemCallResult, message, code));
            IsSuccess(handle,
            {
                ResultMatch(FileDescriptor, Process__allocate_file_descriptor(process, (ProcessIOHandle) handle))
                    IsSuccess(fd, return Success(SystemCallResult, fd));
                    IsError(code, message,
                    {
                        IOContextPtr close_context = IOContext__heap_new(
                            kernel_context, Some(IOSubsystemPtr, io_subsystem),
                            Some(IOHandle, handle), None(ExternalBuffer)
                        );
                        IOSubsystem__close(io_subsystem, close_context);
                        IOContext__dispose(close_context);
                        return Error(SystemCallResult, message, code);
                    });
                End
            })
        End
    }

    if (io_operation == IO_CLOSE)
    {
        FileDescriptor fd = (FileDescriptor) arg2;
        Result(ProcessIOHandle) handle_result = Process__get_io_handle(process, fd);
        ResultMatch(ProcessIOHandle, handle_result)
            IsError(code, message, return Error(SystemCallResult, message, code));
            IsSuccess(handle,
            {
        context = IOContext__heap_new(
            kernel_context, Some(IOSubsystemPtr, io_subsystem), Some(IOHandle, (IOHandle) handle), None(ExternalBuffer)
        );

        Result(IOHandle) close_result = IOSubsystem__close(io_subsystem, context);
        IOContext__dispose(context);

        ResultMatch(IOHandle, close_result)
            IsSuccess(closed,
            {
                Process__remove_file_descriptor(process, fd);
                return Success(SystemCallResult, fd);
            });
            IsError(code, message, return Error(SystemCallResult, message, code));
        End
            })
        End
    }

    if (io_operation == IO_READ)
    {
        FileDescriptor fd = (FileDescriptor) arg2;
        Result(ProcessIOHandle) handle_result = Process__get_io_handle(process, fd);
        ResultMatch(ProcessIOHandle, handle_result)
            IsError(code, message, return Error(SystemCallResult, message, code));
            IsSuccess(handle,
            {
        ExternalBuffer buffer = ExternalBuffer__new((DataPtr) arg3, (size_t) arg4);

        context =
            IOContext__heap_new(
                kernel_context, Some(IOSubsystemPtr, io_subsystem),
                Some(IOHandle, (IOHandle) handle), Some(ExternalBuffer, buffer)
            );

        CompleteCall(ByteCount, context, IOSubsystem__read(io_subsystem, context));
            })
        End
    }

    if (io_operation == IO_LIST_DIR)
    {
        KString path = kstring((const char *) arg2);
        DirectoryEntryPtr entries = (DirectoryEntryPtr) arg3;
        size_t max_entries = (size_t) arg4;

        ResultMatch(DirectoryEntryCount, IOSubsystem__list_dir(io_subsystem, path, entries, max_entries))
            IsSuccess(count, return Success(SystemCallResult, count));
            IsError(code, message, return Error(SystemCallResult, message, code));
        End
    }

    if (io_operation == IO_MKDIR)
    {
        KString path = kstring((const char *) arg2);
        ResultMatch(IOStatus, IOSubsystem__mkdir(io_subsystem, path))
            IsSuccess(status, return Success(SystemCallResult, status));
            IsError(code, message, return Error(SystemCallResult, message, code));
        End
    }

    if (io_operation == IO_REMOVE)
    {
        KString path = kstring((const char *) arg2);
        Flags flags = (Flags) arg3;
        ResultMatch(IOStatus, IOSubsystem__remove(io_subsystem, path, flags))
            IsSuccess(status, return Success(SystemCallResult, status));
            IsError(code, message, return Error(SystemCallResult, message, code));
        End
    }

    if (io_operation == IO_WRITE)
    {
        FileDescriptor fd = (FileDescriptor) arg2;
        Result(ProcessIOHandle) handle_result = Process__get_io_handle(process, fd);
        ResultMatch(ProcessIOHandle, handle_result)
            IsError(code, message, return Error(SystemCallResult, message, code));
            IsSuccess(handle,
            {
        ExternalBuffer buffer = ExternalBuffer__new((DataPtr) arg3, (size_t) arg4);
        context =
            IOContext__heap_new(
                kernel_context, Some(IOSubsystemPtr, io_subsystem),
                Some(IOHandle, (IOHandle) handle), Some(ExternalBuffer, buffer)
            );

        CompleteCall(ByteCount, context, IOSubsystem__write(io_subsystem, context));
            })
        End
    }

    if (io_operation == IO_SERVER_REGISTER)
    {
        KString path = kstring((const char *) arg2);
        IOServerRole role = (IOServerRole) arg3;
        context = IOContext__heap_new(
            kernel_context, Some(IOSubsystemPtr, io_subsystem), None(IOHandle), None(ExternalBuffer)
        );

        Result(IOHandle) register_result = IOSubsystem__server_register(io_subsystem, context, process, path, role);
        IOContext__dispose(context);

        ResultMatch(IOHandle, register_result)
            IsError(code, message, return Error(SystemCallResult, message, code));
            IsSuccess(handle,
            {
                ResultMatch(FileDescriptor, Process__allocate_file_descriptor(process, (ProcessIOHandle) handle))
                    IsSuccess(fd, return Success(SystemCallResult, fd));
                    IsError(code, message, return Error(SystemCallResult, message, code));
                End
            })
        End
    }

    if (io_operation == IO_SERVER_GET_BUFFER)
    {
        FileDescriptor fd = (FileDescriptor) arg2;
        IOServerBufferPtr target = (IOServerBufferPtr) arg3;
        Result(ProcessIOHandle) handle_result = Process__get_io_handle(process, fd);
        ResultMatch(ProcessIOHandle, handle_result)
            IsError(code, message, return Error(SystemCallResult, message, code));
            IsSuccess(handle,
            {
                context = IOContext__heap_new(
                    kernel_context, Some(IOSubsystemPtr, io_subsystem),
                    Some(IOHandle, (IOHandle) handle), None(ExternalBuffer)
                );

                CompleteCall(IOStatus, context, IOSubsystem__server_get_buffer(io_subsystem, context, process, target));
            })
        End
    }

    if (io_operation == IO_SERVER_DATA_AVAILABLE)
    {
        FileDescriptor fd = (FileDescriptor) arg2;
        size_t byte_count = (size_t) arg3;
        Result(ProcessIOHandle) handle_result = Process__get_io_handle(process, fd);
        ResultMatch(ProcessIOHandle, handle_result)
            IsError(code, message, return Error(SystemCallResult, message, code));
            IsSuccess(handle,
            {
                context = IOContext__heap_new(
                    kernel_context, Some(IOSubsystemPtr, io_subsystem),
                    Some(IOHandle, (IOHandle) handle), None(ExternalBuffer)
                );

                Result(IOStatus) status_result = IOSubsystem__server_data_available(io_subsystem, context, byte_count);
                IOContext__dispose(context);
                ResultMatch(IOStatus, status_result)
                    IsSuccess(status,
                    {
                        Scheduler__complete_pending_irqs_for_process(kernel->scheduler, process);
                        return Success(SystemCallResult, status);
                    })
                    IsError(code, message, return Error(SystemCallResult, message, code))
                End
            })
        End
    }

    if (io_operation == IO_SERVER_DATA_CONSUMED)
    {
        FileDescriptor fd = (FileDescriptor) arg2;
        size_t byte_count = (size_t) arg3;
        Result(ProcessIOHandle) handle_result = Process__get_io_handle(process, fd);
        ResultMatch(ProcessIOHandle, handle_result)
            IsError(code, message, return Error(SystemCallResult, message, code));
            IsSuccess(handle,
            {
                context = IOContext__heap_new(
                    kernel_context, Some(IOSubsystemPtr, io_subsystem),
                    Some(IOHandle, (IOHandle) handle), None(ExternalBuffer)
                );

                Result(IOStatus) status_result = IOSubsystem__server_data_consumed(io_subsystem, context, byte_count);
                IOContext__dispose(context);
                ResultMatch(IOStatus, status_result)
                    IsSuccess(status,
                    {
                        Scheduler__complete_pending_irqs_for_process(kernel->scheduler, process);
                        return Success(SystemCallResult, status);
                    })
                    IsError(code, message, return Error(SystemCallResult, message, code))
                End
            })
        End
    }

    if (io_operation == IO_SERVER_EOF)
    {
        FileDescriptor fd = (FileDescriptor) arg2;
        Result(ProcessIOHandle) handle_result = Process__get_io_handle(process, fd);
        ResultMatch(ProcessIOHandle, handle_result)
            IsError(code, message, return Error(SystemCallResult, message, code));
            IsSuccess(handle,
            {
                context = IOContext__heap_new(
                    kernel_context, Some(IOSubsystemPtr, io_subsystem),
                    Some(IOHandle, (IOHandle) handle), None(ExternalBuffer)
                );

                Result(IOStatus) status_result = IOSubsystem__server_eof(io_subsystem, context);
                IOContext__dispose(context);
                ResultMatch(IOStatus, status_result)
                    IsSuccess(status,
                    {
                        Scheduler__complete_pending_irqs_for_process(kernel->scheduler, process);
                        return Success(SystemCallResult, status);
                    })
                    IsError(code, message, return Error(SystemCallResult, message, code))
                End
            })
        End
    }

    return Error(SystemCallResult, KString__sprintf(&unhandled_operation_format, io_operation), SYSCALL_FAILURE);
}
