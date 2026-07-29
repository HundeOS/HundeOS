#include "io_handler.h"
#include "../address.h"
#include "../kernel_core.h"
#include "../kalloc.h"
#include "../arch/syscall.h"
#include "../subsystems/io/block/block_device.h"
#include <stdint.h>


static KString unhandled_operation_format = KSTRING_NEW("Unhandled io operation: %p");
static KString no_process = KSTRING_NEW("No process for IO syscall");
static KString invalid_path_argument = KSTRING_NEW("Invalid path argument");

typedef struct IOOpenCompletionContext
{
    KernelContext kernel_context;
    IOSubsystemPtr io_subsystem;
    ProcessPtr process;
    KString path;
}
IOOpenCompletionContext;

typedef IOOpenCompletionContext * IOOpenCompletionContextPtr;

static Result(Anything) IO__complete_async_open(Result(Anything) open_result, Anything raw_context)
{
    IOOpenCompletionContextPtr context = ptr(raw_context, IOOpenCompletionContext);
    ResultMatch(Anything, open_result)
        IsError(code, message, return Error(Anything, message, code))
        IsSuccess(internal_handle,
        {
            IOContextPtr io_context = IOContext__heap_new(
                context->kernel_context,
                Some(IOSubsystemPtr, context->io_subsystem),
                None(IOHandle),
                None(ExternalBuffer)
            );

            Result(IOHandle) handle_result = IOSubsystem__record_open_handle(
                context->io_subsystem,
                io_context,
                context->path,
                (IOInternalHandle) internal_handle
            );
            IOContext__dispose(io_context);

            ResultMatch(IOHandle, handle_result)
                IsError(code, message, return Error(Anything, message, code))
                IsSuccess(handle,
                {
                    ResultMatch(FileDescriptor, Process__allocate_file_descriptor(context->process, (ProcessIOHandle) handle))
                        IsSuccess(fd, return Success(Anything, fd))
                        IsError(code, message,
                        {
                            IOContextPtr close_context = IOContext__heap_new(
                                context->kernel_context,
                                Some(IOSubsystemPtr, context->io_subsystem),
                                Some(IOHandle, handle),
                                None(ExternalBuffer)
                            );
                            IOSubsystem__close(context->io_subsystem, close_context);
                            IOContext__dispose(close_context);
                            return Error(Anything, message, code);
                        })
                    End
                })
            End
        })
    End

    return Error(Anything, no_process, IO_UNKNOWN_PID);
}

void BlockDevice__set_current_context(KernelContextPtr context) __attribute__((weak));
void BlockDevice__set_current_context(KernelContextPtr context)
{
}

void BlockDevice__clear_current_context() __attribute__((weak));
void BlockDevice__clear_current_context()
{
}

#define CompleteCall(RT, context, block) \
    Result(RT) res = block; \
    BlockDevice__clear_current_context(); \
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
        if (arg2 == 0)
        {
            return Error(SystemCallResult, invalid_path_argument, IO_INVALID_PATH);
        }

        Flags flags = (Flags) arg3;
        KString path = kstring((const char *) arg2);
        IOOpenCompletionContextPtr open_completion_context =
            ptr(Kernel__alloc_kernel(sizeof(IOOpenCompletionContext)), IOOpenCompletionContext);
        *open_completion_context = (IOOpenCompletionContext) {
            .kernel_context = kernel_context,
            .io_subsystem = io_subsystem,
            .process = process,
            .path = path,
        };
        kernel_context.result_mapper = Some(KernelResultMapper, IO__complete_async_open);
        kernel_context.result_mapper_context = (Anything) open_completion_context;

        context =
            IOContext__heap_new(
                kernel_context, Some(IOSubsystemPtr, io_subsystem), None(IOHandle), None(ExternalBuffer)
            );

        BlockDevice__set_current_context(&kernel_context);
        Result(IOHandle) open_result = IOSubsystem__open(io_subsystem, context, path, flags);
        BlockDevice__clear_current_context();
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
        __d_printf(IOSUB, "IO_CLOSE start pid=%i fd=%i\n", process->pid, fd);
        Result(ProcessIOHandle) handle_result = Process__get_io_handle(process, fd);
        ResultMatch(ProcessIOHandle, handle_result)
            IsError(code, message,
            {
                __d_printf(IOSUB, "IO_CLOSE fd lookup failed pid=%i fd=%i code=%i\n", process->pid, fd, code);
                return Error(SystemCallResult, message, code);
            });
            IsSuccess(handle,
            {
        __d_printf(IOSUB, "IO_CLOSE fd lookup success pid=%i fd=%i handle=%i\n", process->pid, fd, handle);
        context = IOContext__heap_new(
            kernel_context, Some(IOSubsystemPtr, io_subsystem), Some(IOHandle, (IOHandle) handle), None(ExternalBuffer)
        );

        __d_printf(IOSUB, "IO_CLOSE calling IOSubsystem__close pid=%i fd=%i handle=%i context=%p\n", process->pid, fd, handle, context);
        BlockDevice__set_current_context(&kernel_context);
        Result(IOHandle) close_result = IOSubsystem__close(io_subsystem, context);
        BlockDevice__clear_current_context();
        __d_printf(IOSUB, "IO_CLOSE IOSubsystem__close returned pid=%i fd=%i handle=%i\n", process->pid, fd, handle);
        IOContext__dispose(context);

        ResultMatch(IOHandle, close_result)
            IsSuccess(closed,
            {
                __d_printf(IOSUB, "IO_CLOSE removing fd pid=%i fd=%i closed=%i\n", process->pid, fd, closed);
                Process__remove_file_descriptor(process, fd);
                __d_printf(IOSUB, "IO_CLOSE success pid=%i fd=%i\n", process->pid, fd);
                return Success(SystemCallResult, fd);
            });
            IsError(code, message,
            {
                __d_printf(IOSUB, "IO_CLOSE failed pid=%i fd=%i code=%i\n", process->pid, fd, code);
                return Error(SystemCallResult, message, code);
            });
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

        BlockDevice__set_current_context(&kernel_context);
        CompleteCall(ByteCount, context, IOSubsystem__read(io_subsystem, context));
            })
        End
    }

    if (io_operation == IO_LIST_DIR)
    {
        if (arg2 == 0 || arg3 == 0)
        {
            return Error(SystemCallResult, invalid_path_argument, IO_INVALID_PATH);
        }

        KString path = kstring((const char *) arg2);
        DirectoryEntryPtr entries = (DirectoryEntryPtr) arg3;
        size_t max_entries = (size_t) arg4;

        BlockDevice__set_current_context(&kernel_context);
        Result(DirectoryEntryCount) list_result = IOSubsystem__list_dir(io_subsystem, path, entries, max_entries);
        BlockDevice__clear_current_context();
        ResultMatch(DirectoryEntryCount, list_result)
            IsSuccess(count, return Success(SystemCallResult, count));
            IsError(code, message, return Error(SystemCallResult, message, code));
        End
    }

    if (io_operation == IO_MKDIR)
    {
        if (arg2 == 0)
        {
            return Error(SystemCallResult, invalid_path_argument, IO_INVALID_PATH);
        }

        KString path = kstring((const char *) arg2);
        BlockDevice__set_current_context(&kernel_context);
        Result(IOStatus) mkdir_result = IOSubsystem__mkdir(io_subsystem, path);
        BlockDevice__clear_current_context();
        ResultMatch(IOStatus, mkdir_result)
            IsSuccess(status, return Success(SystemCallResult, status));
            IsError(code, message, return Error(SystemCallResult, message, code));
        End
    }

    if (io_operation == IO_REMOVE)
    {
        if (arg2 == 0)
        {
            return Error(SystemCallResult, invalid_path_argument, IO_INVALID_PATH);
        }

        KString path = kstring((const char *) arg2);
        Flags flags = (Flags) arg3;
        BlockDevice__set_current_context(&kernel_context);
        Result(IOStatus) remove_result = IOSubsystem__remove(io_subsystem, path, flags);
        BlockDevice__clear_current_context();
        ResultMatch(IOStatus, remove_result)
            IsSuccess(status, return Success(SystemCallResult, status));
            IsError(code, message, return Error(SystemCallResult, message, code));
        End
    }

    if (io_operation == IO_LINK)
    {
        if (arg2 == 0 || arg3 == 0)
        {
            return Error(SystemCallResult, invalid_path_argument, IO_INVALID_PATH);
        }

        KString source_path = kstring((const char *) arg2);
        KString target_path = kstring((const char *) arg3);
        BlockDevice__set_current_context(&kernel_context);
        Result(IOStatus) link_result = IOSubsystem__link(io_subsystem, source_path, target_path);
        BlockDevice__clear_current_context();
        ResultMatch(IOStatus, link_result)
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

        BlockDevice__set_current_context(&kernel_context);
        CompleteCall(ByteCount, context, IOSubsystem__write(io_subsystem, context));
            })
        End
    }

    if (io_operation == IO_SERVER_REGISTER)
    {
        if (arg2 == 0)
        {
            return Error(SystemCallResult, invalid_path_argument, IO_INVALID_PATH);
        }

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
                    IsSuccess(status, return Success(SystemCallResult, status))
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
                    IsSuccess(status, return Success(SystemCallResult, status))
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
                    IsSuccess(status, return Success(SystemCallResult, status))
                    IsError(code, message, return Error(SystemCallResult, message, code))
                End
            })
        End
    }

    return Error(SystemCallResult, KString__sprintf(&unhandled_operation_format, io_operation), SYSCALL_FAILURE);
}
