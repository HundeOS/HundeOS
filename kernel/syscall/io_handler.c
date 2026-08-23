#include "io_handler.h"
#include "../address.h"
#include "../kernel_core.h"
#include "../kalloc.h"
#include "../arch/syscall.h"
#include "../subsystems/io/block/block_device.h"
#include <stdint.h>


static BString unhandled_operation_format = BSTRING_NEW("Unhandled io operation: %p");
static BString no_process = BSTRING_NEW("No process for IO syscall");
static BString invalid_path_argument = BSTRING_NEW("Invalid path argument");

typedef struct IOOpenCompletionContext
{
    KernelContext kernel_context;
    IOSubsystemPtr io_subsystem;
    ProcessPtr process;
    BString path;
}
IOOpenCompletionContext;

typedef IOOpenCompletionContext * IOOpenCompletionContextPtr;

static Result(Anything) IO__complete_async_open(Result(Anything) open_result, Anything raw_context)
{
    IOOpenCompletionContextPtr context = ptr(raw_context, IOOpenCompletionContext);
    match_result(Anything, open_result,
        success(internal_handle)
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

            match_result(IOHandle, handle_result,
                success(handle)
                {
                    match_result(FileDescriptor, Process__allocate_file_descriptor(context->process, (ProcessIOHandle) handle),
                        success(fd) { return Success(Anything, fd); },
                        error(code, message)
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
                        }
                    )
                },
                error(code, message) { return Error(Anything, message, code); }
            )
        },
        error(code, message) { return Error(Anything, message, code); }
    )

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
    match_result(RT, res, /* hundelint: ignore-match-branch-macros */ \
        success(val) \
        { \
            return Success(SystemCallResult, val); \
        }, \
        error(code, message) \
        { \
            return Error(SystemCallResult, message, code); \
        } \
    ) \


Result(SystemCallResult) IO__handler(
    KernelPtr kernel, Optional(ProcessPtr) proc, Anything io_operation,
    Anything arg2, Anything arg3, Anything arg4, Anything arg5
)
{
    IOSubsystemPtr io_subsystem = kernel->io_subsystem;
    Optional(ProcessID) pid = None(ProcessID);
    ProcessPtr process = NULL;
    match_optional(ProcessPtr, proc,
        some(current_process)
        {
            process = current_process;
            pid = Some(ProcessID, process->pid);
        },
        none() {}
    );

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
        BString path = bstring((const char *) arg2);
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

        match_result(IOHandle, open_result,
            success(handle)
            {
            match_result(FileDescriptor, Process__allocate_file_descriptor(process, (ProcessIOHandle) handle),
                success(fd)
                {
                return Success(SystemCallResult, fd);
            },
                error(code, message)
                {
                IOContextPtr close_context = IOContext__heap_new(
                    kernel_context, Some(IOSubsystemPtr, io_subsystem),
                    Some(IOHandle, handle), None(ExternalBuffer)
                );
                IOSubsystem__close(io_subsystem, close_context);
                IOContext__dispose(close_context);
                return Error(SystemCallResult, message, code);
            }
            );
        },
            error(code, message)
            {
            return Error(SystemCallResult, message, code);
        }
        );
    }

    if (io_operation == IO_CLOSE)
    {
        FileDescriptor fd = (FileDescriptor) arg2;
        __d_printf(IOSUB, "IO_CLOSE start pid=%i fd=%i\n", process->pid, fd);
        Result(ProcessIOHandle) handle_result = Process__get_io_handle(process, fd);
        match_result(ProcessIOHandle, handle_result,
            success(handle)
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

            match_result(IOHandle, close_result,
                success(closed)
                {
                __d_printf(IOSUB, "IO_CLOSE removing fd pid=%i fd=%i closed=%i\n", process->pid, fd, closed);
                Process__remove_file_descriptor(process, fd);
                __d_printf(IOSUB, "IO_CLOSE success pid=%i fd=%i\n", process->pid, fd);
                return Success(SystemCallResult, fd);
            },
                error(code, message)
                {
                __d_printf(IOSUB, "IO_CLOSE failed pid=%i fd=%i code=%i\n", process->pid, fd, code);
                return Error(SystemCallResult, message, code);
            }
            );
        },
            error(code, message)
            {
                __d_printf(IOSUB, "IO_CLOSE fd lookup failed pid=%i fd=%i code=%i\n", process->pid, fd, code);
                return Error(SystemCallResult, message, code);
            }
        );
    }

    if (io_operation == IO_READ)
    {
        FileDescriptor fd = (FileDescriptor) arg2;
        Result(ProcessIOHandle) handle_result = Process__get_io_handle(process, fd);
        match_result(ProcessIOHandle, handle_result,
            success(handle)
            {
            ExternalBuffer buffer = ExternalBuffer__new((DataPtr) arg3, (size_t) arg4);

            context =
                IOContext__heap_new(
                    kernel_context, Some(IOSubsystemPtr, io_subsystem),
                    Some(IOHandle, (IOHandle) handle), Some(ExternalBuffer, buffer)
                );

            BlockDevice__set_current_context(&kernel_context);
            CompleteCall(ByteCount, context, IOSubsystem__read(io_subsystem, context));
        },
            error(code, message)
            {
            return Error(SystemCallResult, message, code);
        }
        );
    }

    if (io_operation == IO_LIST_DIR)
    {
        if (arg2 == 0 || arg3 == 0)
        {
            return Error(SystemCallResult, invalid_path_argument, IO_INVALID_PATH);
        }

        BString path = bstring((const char *) arg2);
        DirectoryEntryPtr entries = (DirectoryEntryPtr) arg3;
        size_t max_entries = (size_t) arg4;

        BlockDevice__set_current_context(&kernel_context);
        Result(DirectoryEntryCount) list_result = IOSubsystem__list_dir(io_subsystem, path, entries, max_entries);
        BlockDevice__clear_current_context();
        match_result(DirectoryEntryCount, list_result,
            success(count)
            {
            return Success(SystemCallResult, count);
        },
            error(code, message)
            {
            return Error(SystemCallResult, message, code);
        }
        );
    }

    if (io_operation == IO_MKDIR)
    {
        if (arg2 == 0)
        {
            return Error(SystemCallResult, invalid_path_argument, IO_INVALID_PATH);
        }

        BString path = bstring((const char *) arg2);
        BlockDevice__set_current_context(&kernel_context);
        Result(IOStatus) mkdir_result = IOSubsystem__mkdir(io_subsystem, path);
        BlockDevice__clear_current_context();
        match_result(IOStatus, mkdir_result,
            success(status)
            {
            return Success(SystemCallResult, status);
        },
            error(code, message)
            {
            return Error(SystemCallResult, message, code);
        }
        );
    }

    if (io_operation == IO_REMOVE)
    {
        if (arg2 == 0)
        {
            return Error(SystemCallResult, invalid_path_argument, IO_INVALID_PATH);
        }

        BString path = bstring((const char *) arg2);
        Flags flags = (Flags) arg3;
        BlockDevice__set_current_context(&kernel_context);
        Result(IOStatus) remove_result = IOSubsystem__remove(io_subsystem, path, flags);
        BlockDevice__clear_current_context();
        match_result(IOStatus, remove_result,
            success(status)
            {
            return Success(SystemCallResult, status);
        },
            error(code, message)
            {
            return Error(SystemCallResult, message, code);
        }
        );
    }

    if (io_operation == IO_LINK)
    {
        if (arg2 == 0 || arg3 == 0)
        {
            return Error(SystemCallResult, invalid_path_argument, IO_INVALID_PATH);
        }

        BString source_path = bstring((const char *) arg2);
        BString target_path = bstring((const char *) arg3);
        BlockDevice__set_current_context(&kernel_context);
        Result(IOStatus) link_result = IOSubsystem__link(io_subsystem, source_path, target_path);
        BlockDevice__clear_current_context();
        match_result(IOStatus, link_result,
            success(status)
            {
            return Success(SystemCallResult, status);
        },
            error(code, message)
            {
            return Error(SystemCallResult, message, code);
        }
        );
    }

    if (io_operation == IO_MOUNT)
    {
        if (arg2 == 0 || arg3 == 0 || arg4 == 0)
        {
            return Error(SystemCallResult, invalid_path_argument, IO_INVALID_PATH);
        }

        if (!Process__has_capability(process, PROCESS_CAP_MOUNT_FILESYSTEM))
        {
            return Error(SystemCallResult, bstring("Permission denied"), IO_PERMISSION_DENIED);
        }

        BString device_path = bstring((const char *) arg2);
        BString filesystem_type = bstring((const char *) arg3);
        BString mountpoint = bstring((const char *) arg4);

        BlockDevice__set_current_context(&kernel_context);
        Result(IOStatus) mount_result = IOSubsystem__mount(io_subsystem, device_path, filesystem_type, mountpoint);
        BlockDevice__clear_current_context();
        match_result(IOStatus, mount_result,
            success(status)
            {
            return Success(SystemCallResult, status);
        },
            error(code, message)
            {
            return Error(SystemCallResult, message, code);
        }
        );
    }

    if (io_operation == IO_WRITE)
    {
        FileDescriptor fd = (FileDescriptor) arg2;
        Result(ProcessIOHandle) handle_result = Process__get_io_handle(process, fd);
        match_result(ProcessIOHandle, handle_result,
            success(handle)
            {
            ExternalBuffer buffer = ExternalBuffer__new((DataPtr) arg3, (size_t) arg4);
            context =
                IOContext__heap_new(
                    kernel_context, Some(IOSubsystemPtr, io_subsystem),
                    Some(IOHandle, (IOHandle) handle), Some(ExternalBuffer, buffer)
                );

            BlockDevice__set_current_context(&kernel_context);
            CompleteCall(ByteCount, context, IOSubsystem__write(io_subsystem, context));
        },
            error(code, message)
            {
            return Error(SystemCallResult, message, code);
        }
        );
    }

    if (io_operation == IO_SERVER_REGISTER)
    {
        if (arg2 == 0)
        {
            return Error(SystemCallResult, invalid_path_argument, IO_INVALID_PATH);
        }

        BString path = bstring((const char *) arg2);
        IOServerRole role = (IOServerRole) arg3;
        context = IOContext__heap_new(
            kernel_context, Some(IOSubsystemPtr, io_subsystem), None(IOHandle), None(ExternalBuffer)
        );

        Result(IOHandle) register_result = IOSubsystem__server_register(io_subsystem, context, process, path, role);
        IOContext__dispose(context);

        match_result(IOHandle, register_result,
            success(handle)
            {
            match_result(FileDescriptor, Process__allocate_file_descriptor(process, (ProcessIOHandle) handle),
                success(fd)
                {
                return Success(SystemCallResult, fd);
            },
                error(code, message)
                {
                return Error(SystemCallResult, message, code);
            }
            );
        },
            error(code, message)
            {
            return Error(SystemCallResult, message, code);
        }
        );
    }

    if (io_operation == IO_SERVER_GET_BUFFER)
    {
        FileDescriptor fd = (FileDescriptor) arg2;
        IOServerBufferPtr target = (IOServerBufferPtr) arg3;
        Result(ProcessIOHandle) handle_result = Process__get_io_handle(process, fd);
        match_result(ProcessIOHandle, handle_result,
            success(handle)
            {
                context = IOContext__heap_new(
                    kernel_context, Some(IOSubsystemPtr, io_subsystem),
                    Some(IOHandle, (IOHandle) handle), None(ExternalBuffer)
                );

                CompleteCall(IOStatus, context, IOSubsystem__server_get_buffer(io_subsystem, context, process, target));
        },
            error(code, message)
            {
            return Error(SystemCallResult, message, code);
        }
        );
    }

    if (io_operation == IO_SERVER_DATA_AVAILABLE)
    {
        FileDescriptor fd = (FileDescriptor) arg2;
        size_t byte_count = (size_t) arg3;
        Result(ProcessIOHandle) handle_result = Process__get_io_handle(process, fd);
        match_result(ProcessIOHandle, handle_result,
            success(handle)
            {
                context = IOContext__heap_new(
                    kernel_context, Some(IOSubsystemPtr, io_subsystem),
                    Some(IOHandle, (IOHandle) handle), None(ExternalBuffer)
                );

                Result(IOStatus) status_result = IOSubsystem__server_data_available(io_subsystem, context, byte_count);
                IOContext__dispose(context);
                match_result(IOStatus, status_result,
                    success(status) { return Success(SystemCallResult, status); },
                    error(code, message) { return Error(SystemCallResult, message, code); }
                )
        },
            error(code, message)
            {
            return Error(SystemCallResult, message, code);
        }
        );
    }

    if (io_operation == IO_SERVER_DATA_CONSUMED)
    {
        FileDescriptor fd = (FileDescriptor) arg2;
        size_t byte_count = (size_t) arg3;
        Result(ProcessIOHandle) handle_result = Process__get_io_handle(process, fd);
        match_result(ProcessIOHandle, handle_result,
            success(handle)
            {
                context = IOContext__heap_new(
                    kernel_context, Some(IOSubsystemPtr, io_subsystem),
                    Some(IOHandle, (IOHandle) handle), None(ExternalBuffer)
                );

                Result(IOStatus) status_result = IOSubsystem__server_data_consumed(io_subsystem, context, byte_count);
                IOContext__dispose(context);
                match_result(IOStatus, status_result,
                    success(status) { return Success(SystemCallResult, status); },
                    error(code, message) { return Error(SystemCallResult, message, code); }
                )
        },
            error(code, message)
            {
            return Error(SystemCallResult, message, code);
        }
        );
    }

    if (io_operation == IO_SERVER_EOF)
    {
        FileDescriptor fd = (FileDescriptor) arg2;
        Result(ProcessIOHandle) handle_result = Process__get_io_handle(process, fd);
        match_result(ProcessIOHandle, handle_result,
            success(handle)
            {
                context = IOContext__heap_new(
                    kernel_context, Some(IOSubsystemPtr, io_subsystem),
                    Some(IOHandle, (IOHandle) handle), None(ExternalBuffer)
                );

                Result(IOStatus) status_result = IOSubsystem__server_eof(io_subsystem, context);
                IOContext__dispose(context);
                match_result(IOStatus, status_result,
                    success(status) { return Success(SystemCallResult, status); },
                    error(code, message) { return Error(SystemCallResult, message, code); }
                )
        },
            error(code, message)
            {
            return Error(SystemCallResult, message, code);
        }
        );
    }

    return Error(SystemCallResult, BString__sprintf(&unhandled_operation_format, io_operation), SYSCALL_FAILURE);
}
