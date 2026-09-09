#include "io_handler.h"
#include "../address.h"
#include "../kernel_core.h"
#include "../kalloc.h"
#include "../arch/syscall.h"
#include "../subsystems/io/block/block_device.h"
#include <stdint.h>

static BString unhandled_operation_format = bstr("Unhandled io operation: %p");
static BString no_process = bstr("No process for IO syscall");
static BString invalid_path_argument = bstr("Invalid path argument");

typedef struct IOOpenCompletionContext
{
    KernelContext kernel_context;
    IOSubsystemPtr io_subsystem;
    ProcessPtr process;
    BString path;
} IOOpenCompletionContext;

typedef IOOpenCompletionContext * IOOpenCompletionContextPtr;

typedef struct IOAcceptCompletionContext
{
    KernelContext kernel_context;
    IOSubsystemPtr io_subsystem;
    ProcessPtr process;
} IOAcceptCompletionContext;

typedef IOAcceptCompletionContext * IOAcceptCompletionContextPtr;

static IOOpenCompletionContext IOOpenCompletionContext__new(const KernelContext * kernel_context,
                                                            IOSubsystemPtr io_subsystem, ProcessPtr process,
                                                            ConstBStringPtr path)
{
    return (IOOpenCompletionContext){
        .kernel_context = *kernel_context,
        .io_subsystem = io_subsystem,
        .process = process,
        .path = *path,
    };
}

static IOAcceptCompletionContext IOAcceptCompletionContext__new(const KernelContext * kernel_context,
                                                                IOSubsystemPtr io_subsystem, ProcessPtr process)
{
    return (IOAcceptCompletionContext){
        .kernel_context = *kernel_context,
        .io_subsystem = io_subsystem,
        .process = process,
    };
}

static IOContextPtr IO__heap_context(KernelContextPtr kernel_context, IOSubsystemPtr io_subsystem,
                                     OptionalPtr(IOHandle) handle, OptionalPtr(ExternalBuffer) buffer)
{
    return IOContext__heap_new(kernel_context, &Some(IOSubsystemPtr, io_subsystem), handle, buffer);
}

static IOContextPtr IO__heap_context_without_handle(KernelContextPtr kernel_context, IOSubsystemPtr io_subsystem)
{
    return IO__heap_context(kernel_context, io_subsystem, &None(IOHandle), &None(ExternalBuffer));
}

static IOContextPtr IO__heap_context_with_handle(KernelContextPtr kernel_context, IOSubsystemPtr io_subsystem,
                                                 IOHandle handle)
{
    return IO__heap_context(kernel_context, io_subsystem, &Some(IOHandle, handle), &None(ExternalBuffer));
}

static IOContextPtr IO__heap_context_with_handle_and_buffer(KernelContextPtr kernel_context,
                                                            IOSubsystemPtr io_subsystem, IOHandle handle,
                                                            ConstExternalBufferPtr buffer)
{
    return IO__heap_context(kernel_context, io_subsystem, &Some(IOHandle, handle), &Some(ExternalBuffer, *buffer));
}

static Result(Anything) IO__allocate_fd_for_handle(KernelContextPtr kernel_context, IOSubsystemPtr io_subsystem,
                                                   ProcessPtr process, IOHandle handle)
{
    match_result(
        Process__allocate_file_descriptor(process, (ProcessIOHandle)handle),
        success(fd) { return Success(Anything, fd); },
        error(code, message)
        {
            IOContextPtr close_context = IO__heap_context_with_handle(kernel_context, io_subsystem, handle);
            IOSubsystem__close(io_subsystem, close_context);
            IOContext__dispose(close_context);
            return Error(Anything, message, code);
        }
    );
}

static Result(Anything) IO__complete_async_open(ResultPtr(Anything) open_result, Anything raw_context)
{
    IOOpenCompletionContextPtr context = ptr(raw_context, IOOpenCompletionContext);
    match_result(
        *open_result,
        success(internal_handle)
        {
            IOContextPtr io_context = IO__heap_context_without_handle(&context->kernel_context, context->io_subsystem);

            Result(IOHandle) handle_result = IOSubsystem__record_open_handle(
                context->io_subsystem, io_context, &context->path, (IOInternalHandle)internal_handle);
            IOContext__dispose(io_context);

            match_result(
                handle_result,
                success(handle)
                {
                    return IO__allocate_fd_for_handle(&context->kernel_context, context->io_subsystem, context->process,
                                                      handle);
                },
                error(code, message) { return Error(Anything, message, code); }
            );
        },
        error(code, message) { return Error(Anything, message, code); }
    );

    return Error(Anything, no_process, IO_UNKNOWN_PID);
}

static Result(Anything) IO__complete_async_accept(ResultPtr(Anything) accept_result, Anything raw_context)
{
    IOAcceptCompletionContextPtr context = ptr(raw_context, IOAcceptCompletionContext);
    Result(Anything) result = Error(Anything, no_process, IO_UNKNOWN_PID);

    match_result(
        *accept_result,
        success(handle)
        {
            result = IO__allocate_fd_for_handle(&context->kernel_context, context->io_subsystem, context->process,
                                                (IOHandle)handle);
        },
        error(code, message) { result = Error(Anything, message, code); }
    );

    Kernel__free(addr(context));
    return result;
}

void BlockDevice__set_current_context(KernelContextPtr context) __attribute__((weak));
void BlockDevice__set_current_context(KernelContextPtr context)
{
}

void BlockDevice__clear_current_context() __attribute__((weak));
void BlockDevice__clear_current_context()
{
}

#define CompleteCall(RT, context, block)                                                                               \
    Result(RT) res = block;                                                                                            \
    BlockDevice__clear_current_context();                                                                              \
    IOContext__dispose(context); \
    match_result(res, /* hundelint: ignore-match-branch-macros */                                                                                     \
        success(val) { return Success(SystemCallResult, val); },                                                       \
        error(code, message) { return Error(SystemCallResult, message, code); });

Result(SystemCallResult) IO__handler(KernelPtr kernel, OptionalPtr(ProcessPtr) proc, Anything io_operation,
                                     Anything arg2, Anything arg3, Anything arg4, Anything arg5)
{
    IOSubsystemPtr io_subsystem = kernel->io_subsystem;
    Optional(ProcessID) pid = None(ProcessID);
    ProcessPtr process = NULL;
    match_optional(
        *proc,
        some(current_process)
        {
            process = current_process;
            pid = Some(ProcessID, process->pid);
        },
        none_do_nothing()
    );

    if (process == NULL)
    {
        return Error(SystemCallResult, no_process, IO_UNKNOWN_PID);
    }

    KernelContext kernel_context = KernelContext__new(&pid, &Some(SchedulerPtr, kernel->scheduler));
    IOContextPtr context = NULL;

    if (io_operation == IO_OPEN)
    {
        IOPathRequestPtr request = (IOPathRequestPtr)arg2;
        if (arg2 == 0)
        {
            return Error(SystemCallResult, invalid_path_argument, IO_INVALID_PATH);
        }

        IOOpenCompletionContextPtr open_completion_context =
            ptr(Kernel__alloc_kernel(sizeof(IOOpenCompletionContext)), IOOpenCompletionContext);
        *open_completion_context = IOOpenCompletionContext__new(&kernel_context, io_subsystem, process, &request->path);
        kernel_context.result_mapper = Some(KernelResultMapper, IO__complete_async_open);
        kernel_context.result_mapper_context = (Anything)open_completion_context;

        context = IO__heap_context_without_handle(&kernel_context, io_subsystem);

        BlockDevice__set_current_context(&kernel_context);
        Result(IOHandle) open_result = IOSubsystem__open(io_subsystem, context, &request->path, request->flags);
        BlockDevice__clear_current_context();
        IOContext__dispose(context);

        match_result(
            open_result,
            success(handle)
            {
                match_result(
                    Process__allocate_file_descriptor(process, (ProcessIOHandle)handle),
                    success(fd) { return Success(SystemCallResult, fd); },
                    error(code, message)
                    {
                        IOContextPtr close_context =
                            IO__heap_context_with_handle(&kernel_context, io_subsystem, handle);
                        IOSubsystem__close(io_subsystem, close_context);
                        IOContext__dispose(close_context);
                        return Error(SystemCallResult, message, code);
                    }
                );
            },
            error(code, message) { return Error(SystemCallResult, message, code); }
        );
    }

    if (io_operation == IO_CLOSE)
    {
        FileDescriptor fd = (FileDescriptor)arg2;
        __d_printf(IOSUB, "IO_CLOSE start pid=%i fd=%i\n", process->pid, fd);
        Result(ProcessIOHandle) handle_result = Process__get_io_handle(process, fd);
        match_result(
            handle_result,
            success(handle)
            {
                __d_printf(IOSUB, "IO_CLOSE fd lookup success pid=%i fd=%i handle=%i\n", process->pid, fd, handle);
                context = IO__heap_context_with_handle(&kernel_context, io_subsystem, (IOHandle)handle);

                __d_printf(IOSUB, "IO_CLOSE calling IOSubsystem__close pid=%i fd=%i handle=%i context=%p\n",
                           process->pid, fd, handle, context);
                BlockDevice__set_current_context(&kernel_context);
                Result(IOHandle) close_result = IOSubsystem__close(io_subsystem, context);
                BlockDevice__clear_current_context();
                __d_printf(IOSUB, "IO_CLOSE IOSubsystem__close returned pid=%i fd=%i handle=%i\n", process->pid, fd,
                           handle);
                IOContext__dispose(context);

                match_result(
                    close_result,
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

    if (io_operation == IO_ACCEPT)
    {
        FileDescriptor fd = (FileDescriptor)arg2;
        Result(ProcessIOHandle) handle_result = Process__get_io_handle(process, fd);
        match_result(
            handle_result,
            success(handle)
            {
                IOAcceptCompletionContextPtr accept_completion_context =
                    ptr(Kernel__alloc_kernel(sizeof(IOAcceptCompletionContext)), IOAcceptCompletionContext);
                *accept_completion_context = IOAcceptCompletionContext__new(&kernel_context, io_subsystem, process);
                kernel_context.result_mapper = Some(KernelResultMapper, IO__complete_async_accept);
                kernel_context.result_mapper_context = (Anything)accept_completion_context;

                context = IO__heap_context_with_handle(&kernel_context, io_subsystem, (IOHandle)handle);

                Result(IOHandle) accept_result = IOSubsystem__accept(io_subsystem, context);
                IOContext__dispose(context);
                match_result(
                    accept_result,
                    success(accepted_handle)
                    {
                        Kernel__free(addr(accept_completion_context));
                        match_result(
                            IO__allocate_fd_for_handle(&kernel_context, io_subsystem, process, accepted_handle),
                            success(accepted_fd) { return Success(SystemCallResult, accepted_fd); },
                            error(code, message) { return Error(SystemCallResult, message, code); }
                        );
                    },
                    error(code, message)
                    {
                        if (code != IO_WAIT_FOR_DATA)
                        {
                            Kernel__free(addr(accept_completion_context));
                        }
                        return Error(SystemCallResult, message, code);
                    }
                );
            },
            error(code, message) { return Error(SystemCallResult, message, code); }
        );
    }

    if (io_operation == IO_READ)
    {
        FileDescriptor fd = (FileDescriptor)arg2;
        Result(ProcessIOHandle) handle_result = Process__get_io_handle(process, fd);
        match_result(
            handle_result,
            success(handle)
            {
                ExternalBuffer buffer = ExternalBuffer__new((DataPtr)arg3, (size_t)arg4);

                context =
                    IO__heap_context_with_handle_and_buffer(&kernel_context, io_subsystem, (IOHandle)handle, &buffer);

                BlockDevice__set_current_context(&kernel_context);
                CompleteCall(ByteCount, context, IOSubsystem__read(io_subsystem, context));
            },
            error(code, message) { return Error(SystemCallResult, message, code); }
        );
    }

    if (io_operation == IO_LIST_DIR)
    {
        IOListDirRequestPtr request = (IOListDirRequestPtr)arg2;
        if (arg2 == 0 || request->entries == NULL)
        {
            return Error(SystemCallResult, invalid_path_argument, IO_INVALID_PATH);
        }

        BlockDevice__set_current_context(&kernel_context);
        Result(DirectoryEntryCount) list_result =
            IOSubsystem__list_dir(io_subsystem, &request->path, request->entries, request->max_entries);
        BlockDevice__clear_current_context();
        match_result(
            list_result,
            success(count) { return Success(SystemCallResult, count); },
            error(code, message) { return Error(SystemCallResult, message, code); }
        );
    }

    if (io_operation == IO_MKDIR)
    {
        IOPathRequestPtr request = (IOPathRequestPtr)arg2;
        if (arg2 == 0)
        {
            return Error(SystemCallResult, invalid_path_argument, IO_INVALID_PATH);
        }

        BlockDevice__set_current_context(&kernel_context);
        Result(IOStatus) mkdir_result = IOSubsystem__mkdir(io_subsystem, &request->path);
        BlockDevice__clear_current_context();
        match_result(
            mkdir_result,
            success(status) { return Success(SystemCallResult, status); },
            error(code, message) { return Error(SystemCallResult, message, code); }
        );
    }

    if (io_operation == IO_REMOVE)
    {
        IOPathRequestPtr request = (IOPathRequestPtr)arg2;
        if (arg2 == 0)
        {
            return Error(SystemCallResult, invalid_path_argument, IO_INVALID_PATH);
        }

        BlockDevice__set_current_context(&kernel_context);
        Result(IOStatus) remove_result = IOSubsystem__remove(io_subsystem, &request->path, request->flags);
        BlockDevice__clear_current_context();
        match_result(
            remove_result,
            success(status) { return Success(SystemCallResult, status); },
            error(code, message) { return Error(SystemCallResult, message, code); }
        );
    }

    if (io_operation == IO_LINK)
    {
        IOLinkRequestPtr request = (IOLinkRequestPtr)arg2;
        if (arg2 == 0)
        {
            return Error(SystemCallResult, invalid_path_argument, IO_INVALID_PATH);
        }

        BlockDevice__set_current_context(&kernel_context);
        Result(IOStatus) link_result = IOSubsystem__link(io_subsystem, &request->source_path, &request->target_path);
        BlockDevice__clear_current_context();
        match_result(
            link_result,
            success(status) { return Success(SystemCallResult, status); },
            error(code, message) { return Error(SystemCallResult, message, code); }
        );
    }

    if (io_operation == IO_MOUNT)
    {
        IOMountRequestPtr request = (IOMountRequestPtr)arg2;
        if (arg2 == 0)
        {
            return Error(SystemCallResult, invalid_path_argument, IO_INVALID_PATH);
        }

        if (!Process__has_capability(process, PROCESS_CAP_MOUNT_FILESYSTEM))
        {
            return Error(SystemCallResult, bstring("Permission denied"), IO_PERMISSION_DENIED);
        }

        BlockDevice__set_current_context(&kernel_context);
        Result(IOStatus) mount_result =
            IOSubsystem__mount(io_subsystem, &request->device_path, &request->filesystem_type, &request->mountpoint);
        BlockDevice__clear_current_context();
        match_result(
            mount_result,
            success(status) { return Success(SystemCallResult, status); },
            error(code, message) { return Error(SystemCallResult, message, code); }
        );
    }

    if (io_operation == IO_WRITE)
    {
        FileDescriptor fd = (FileDescriptor)arg2;
        Result(ProcessIOHandle) handle_result = Process__get_io_handle(process, fd);
        match_result(
            handle_result,
            success(handle)
            {
                ExternalBuffer buffer = ExternalBuffer__new((DataPtr)arg3, (size_t)arg4);
                context =
                    IO__heap_context_with_handle_and_buffer(&kernel_context, io_subsystem, (IOHandle)handle, &buffer);

                BlockDevice__set_current_context(&kernel_context);
                CompleteCall(ByteCount, context, IOSubsystem__write(io_subsystem, context));
            },
            error(code, message) { return Error(SystemCallResult, message, code); }
        );
    }

    if (io_operation == IO_SERVER_REGISTER)
    {
        IOServerRegisterRequestPtr request = (IOServerRegisterRequestPtr)arg2;
        if (arg2 == 0)
        {
            return Error(SystemCallResult, invalid_path_argument, IO_INVALID_PATH);
        }

        context = IO__heap_context_without_handle(&kernel_context, io_subsystem);

        Result(IOHandle) register_result =
            IOSubsystem__server_register(io_subsystem, context, process, &request->path, request->role);
        IOContext__dispose(context);

        match_result(
            register_result,
            success(handle)
            {
                match_result(
                    Process__allocate_file_descriptor(process, (ProcessIOHandle)handle),
                    success(fd) { return Success(SystemCallResult, fd); },
                    error(code, message) { return Error(SystemCallResult, message, code); }
                );
            },
            error(code, message) { return Error(SystemCallResult, message, code); }
        );
    }

    if (io_operation == IO_SERVER_GET_BUFFER)
    {
        FileDescriptor fd = (FileDescriptor)arg2;
        IOServerBufferPtr target = (IOServerBufferPtr)arg3;
        Result(ProcessIOHandle) handle_result = Process__get_io_handle(process, fd);
        match_result(
            handle_result,
            success(handle)
            {
                context = IO__heap_context_with_handle(&kernel_context, io_subsystem, (IOHandle)handle);

                CompleteCall(IOStatus, context, IOSubsystem__server_get_buffer(io_subsystem, context, process, target));
            },
            error(code, message) { return Error(SystemCallResult, message, code); }
        );
    }

    if (io_operation == IO_SERVER_DATA_AVAILABLE)
    {
        FileDescriptor fd = (FileDescriptor)arg2;
        size_t byte_count = (size_t)arg3;
        Result(ProcessIOHandle) handle_result = Process__get_io_handle(process, fd);
        match_result(
            handle_result,
            success(handle)
            {
                context = IO__heap_context_with_handle(&kernel_context, io_subsystem, (IOHandle)handle);

                Result(IOStatus) status_result = IOSubsystem__server_data_available(io_subsystem, context, byte_count);
                IOContext__dispose(context);
                match_result(
                    status_result,
                    success(status) { return Success(SystemCallResult, status); },
                    error(code, message) { return Error(SystemCallResult, message, code); }
                );
            },
            error(code, message) { return Error(SystemCallResult, message, code); }
        );
    }

    if (io_operation == IO_SERVER_DATA_CONSUMED)
    {
        FileDescriptor fd = (FileDescriptor)arg2;
        size_t byte_count = (size_t)arg3;
        Result(ProcessIOHandle) handle_result = Process__get_io_handle(process, fd);
        match_result(
            handle_result,
            success(handle)
            {
                context = IO__heap_context_with_handle(&kernel_context, io_subsystem, (IOHandle)handle);

                Result(IOStatus) status_result = IOSubsystem__server_data_consumed(io_subsystem, context, byte_count);
                IOContext__dispose(context);
                match_result(
                    status_result,
                    success(status) { return Success(SystemCallResult, status); },
                    error(code, message) { return Error(SystemCallResult, message, code); }
                );
            },
            error(code, message) { return Error(SystemCallResult, message, code); }
        );
    }

    if (io_operation == IO_SERVER_EOF)
    {
        FileDescriptor fd = (FileDescriptor)arg2;
        Result(ProcessIOHandle) handle_result = Process__get_io_handle(process, fd);
        match_result(
            handle_result,
            success(handle)
            {
                context = IO__heap_context_with_handle(&kernel_context, io_subsystem, (IOHandle)handle);

                Result(IOStatus) status_result = IOSubsystem__server_eof(io_subsystem, context);
                IOContext__dispose(context);
                match_result(
                    status_result,
                    success(status) { return Success(SystemCallResult, status); },
                    error(code, message) { return Error(SystemCallResult, message, code); }
                );
            },
            error(code, message) { return Error(SystemCallResult, message, code); }
        );
    }

    return Error(SystemCallResult, BString__sprintf(&unhandled_operation_format, io_operation), SYSCALL_FAILURE);
}
