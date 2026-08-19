#include "exec.h"

#include "../../common_includes/syscalls/io.h"
#include "../../common_includes/syscalls/spawn.h"
#include "../types/result.h"
#include "../bin/elf.h"
#include "../kio/io.h"
#include "../kalloc.h"
#include "../syscall/deferred.h"

FixedArrayBodyDefine(ElfReadBuffer);

static BString unreachable_code = BSTRING_NEW("unreachable code");
static BString permission_denied_message = BSTRING_NEW("Permission denied");
static BString incomplete_elf_message = BSTRING_NEW("Incomplete ELF");
static const BString drivers_prefix = BSTRING_NEW("/sbin/drivers/");
static const BString hundeschau_program = BSTRING_NEW("/sbin/hundeschau");
static const BString hundeschau_boot_program = BSTRING_NEW("hundeschau");
static const BString hundebus_program = BSTRING_NEW("/sbin/drivers/hundebus");
static const BString virtio_blk_program = BSTRING_NEW("/sbin/drivers/virtio_blk");
static const BString uart_rx_program = BSTRING_NEW("/sbin/drivers/uart_rx");
static const BString uart_tx_program = BSTRING_NEW("/sbin/drivers/uart_tx");

static bool Exec__is_hund_owned_program(ConstBStringPtr path)
{
    return BString__equals(path, &hundeschau_program)
        || BString__equals(path, &hundeschau_boot_program)
        || BString__equals(path, &hundebus_program)
        || BString__equals(path, &virtio_blk_program)
        || BString__equals(path, &uart_rx_program)
        || BString__equals(path, &uart_tx_program);
}

void Exec__apply_builtin_identity_policy(ProcessPtr self)
{
    if (Exec__is_hund_owned_program(&self->name))
    {
        Process__set_hund_identity(self);
    }
}

static bool Exec__can_execute(OriginatingContext context, ConstBStringPtr path)
{
    if (!BString__starts_with(path, &drivers_prefix))
    {
        return true;
    }

    Match(ProcessPtr, context.process)
        IsSome(process, return process->group_id == PROCESS_GROUP_HUND)
    End

    return true;
}

static bool Exec__can_spawn_as(OriginatingContext context, UserID requested_user_id)
{
    Match(ProcessPtr, context.process)
        IsSome(process,
        {
            if (Process__is_root(process))
            {
                return true;
            }

            if (process->group_id == PROCESS_GROUP_HUND)
            {
                return requested_user_id == PROCESS_USER_HUND || requested_user_id == PROCESS_USER_USER;
            }

            return process->user_id == requested_user_id;
        })
    End

    return false;
}

static void Exec__apply_requested_identity(ProcessPtr self, Optional(UserID) requested_user_id)
{
    Match(UserID, requested_user_id)
        IsSome(user_id,
        {
            switch (user_id)
            {
                case PROCESS_USER_ROOT:
                    Process__set_root_identity(self);
                    return;
                case PROCESS_USER_HUND:
                    Process__set_hund_identity(self);
                    return;
                case PROCESS_USER_USER:
                    Process__set_user_identity(self);
                    return;
                default:
                    return;
            }
        })
    End
}

static Result(ProcessPtr) Exec__launch(
    OriginatingContext context,
    Elf elf,
    bool wait,
    Optional(UserID) requested_user_id,
    ProcessArguments arguments,
    SpawnStdio stdio
)
{
    __d_printf(EXEC, "Launching elf %s\n", cstr(elf.name));
    KernelPtr kernel = context.kernel;
    Result(ProcessPtr) proc = Process__setup_with_args(kernel, elf, context.process, arguments);
    ResultMatch(ProcessPtr, proc)
        IsSuccess(process,
        {
            ProcessPtr new_proc = process;
            Exec__apply_requested_identity(new_proc, requested_user_id);
            Exec__apply_builtin_identity_policy(new_proc);
            if (SpawnStdio__is_present(stdio))
            {
                Match(ProcessPtr, context.process)
                    IsSome(parent,
                    {
                        ResultMatch(FileDescriptor, Process__copy_file_descriptor(
                            new_proc, parent, 0, (FileDescriptor) SpawnStdio__stdin_fd(stdio)
                        ))
                            IsError(code, message, return Error(ProcessPtr, message, SPAWN_FAILED))
                        End
                        ResultMatch(FileDescriptor, Process__copy_file_descriptor(
                            new_proc, parent, 1, (FileDescriptor) SpawnStdio__stdout_fd(stdio)
                        ))
                            IsError(code, message, return Error(ProcessPtr, message, SPAWN_FAILED))
                        End
                        ResultMatch(FileDescriptor, Process__copy_file_descriptor(
                            new_proc, parent, 2, (FileDescriptor) SpawnStdio__stderr_fd(stdio)
                        ))
                            IsError(code, message, return Error(ProcessPtr, message, SPAWN_FAILED))
                        End
                    })
                    IsNone(return Error(ProcessPtr, bstring("No parent process for stdio"), SPAWN_FAILED))
                End
            }
            Match(ProcessPtr, context.process)
                IsSome(parent,
                {
                    __d_printf(EXEC, "Parent %p\n", parent);
                    Result(CPUID) schedule_result = wait
                        ? Scheduler__cpu_for_process(kernel->scheduler, parent)
                        : Scheduler__schedule(kernel->scheduler, new_proc);
                    if (wait)
                    {
                        ResultMatch(CPUID, schedule_result)
                            IsSuccess(parent_cpu,
                            {
                                schedule_result = Scheduler__schedule_on_cpu(kernel->scheduler, new_proc, parent_cpu);
                            })
                            IsError(code, message, return Error(ProcessPtr, message, SPAWN_FAILED))
                        End
                    }

                    ResultMatch(CPUID, schedule_result)
                        IsError(code, message, return Error(ProcessPtr, message, SPAWN_FAILED))
                    End

                    if (wait)
                    {
                        __d_printf(
                            EXEC,
                            "Parking parent pid=%i name=%s waiting_for_pid=%i name=%s\n",
                            parent->pid,
                            cstr(parent->name),
                            new_proc->pid,
                            cstr(new_proc->name)
                        );
                        // TODO we need a continuation to give the result of the proc back to the parent
                        KernelContextPtr kernel_context = KernelContext__from_originating_context_heap(context);

                        Scheduler__park_with_context_ownership(
                            kernel->scheduler, parent, kernel_context,
                            Some(Continuation, SystemCall__Complete), PARK_WAIT_CHILD, true
                        );
                        __d_printf(
                            EXEC,
                            "Parent parked pid=%i child_pid=%i\n",
                            parent->pid,
                            new_proc->pid
                        );
                    }
                    __d_printf(EXEC, "Scheduling elf %s\n", cstr(elf.name));


                    return Success(ProcessPtr, new_proc);
                })
            End
        })
        IsError(code, message,
        {
            return Error(ProcessPtr, message, SPAWN_FAILED);
        })
    End
    return Error(ProcessPtr, unreachable_code, SPAWN_FAILED);
}

static Optional(ByteCount) Exec__required_elf_size(DataPtr data, ByteCount bytes_read)
{
    if (bytes_read < (ByteCount) sizeof(Elf64Header))
    {
        return None(ByteCount);
    }

    Elf64Header * header = (Elf64Header *) data;
    if (header->program_header_entry_size != sizeof(ProgramHeader))
    {
        return None(ByteCount);
    }

    ByteCount program_headers_end =
        (ByteCount) header->program_header_offset +
        ((ByteCount) header->program_header_entries * (ByteCount) header->program_header_entry_size);
    if (program_headers_end < (ByteCount) header->program_header_offset || program_headers_end > bytes_read)
    {
        return None(ByteCount);
    }

    ByteCount required_size = program_headers_end;
    ProgramHeaderPtr headers = (ProgramHeaderPtr) (data + header->program_header_offset);
    for (uint16_t index = 0; index < header->program_header_entries; index++)
    {
        ProgramHeaderPtr program_header = &headers[index];
        if (program_header->type == Load)
        {
            ByteCount segment_end = (ByteCount) program_header->segment_offset + (ByteCount) program_header->file_size;
            if (segment_end < (ByteCount) program_header->segment_offset)
            {
                return None(ByteCount);
            }
            if (segment_end > required_size)
            {
                required_size = segment_end;
            }
        }
    }

    return Some(ByteCount, required_size);
}

static Result(ByteCount) Exec__read_remaining_elf(
    OriginatingContext context,
    IOHandle handle,
    Address elf_start,
    ByteCount bytes_read,
    ByteCount required_size)
{
    ByteCount total_read = bytes_read;
    while (total_read < required_size)
    {
        size_t remaining = (size_t) (required_size - total_read);
        size_t read_size = remaining < DataBuffer__max_size ? remaining : DataBuffer__max_size;
        ResultMatch(ByteCount, read_into(context, handle, ptr(elf_start + total_read, Data), read_size))
            IsSuccess(chunk_size,
            {
                if (chunk_size <= 0)
                {
                    return Error(ByteCount, incomplete_elf_message, SPAWN_FAILED);
                }
                total_read += chunk_size;
            })
            IsError(code, message, return Error(ByteCount, message, code))
        End
    }

    return Success(ByteCount, total_read);
}

static Result(Elf) Exec__read_elf(OriginatingContext context, IOHandle handle, BString path)
{
    DataBufferPtr buffer = DataBuffer__heap_new();
    __d_println(EXEC, "Reading elf");
    ResultMatch(ByteCount, read(context, handle, buffer))
        IsSuccess(bytes_read,
        {
            ByteCount required_size = bytes_read;
            DataPtr elf_data = DataBuffer__data(buffer);
            Match(ByteCount, Exec__required_elf_size(elf_data, bytes_read))
                IsSome(size, required_size = size)
            End

            __d_printf(EXEC, "Copying elf %i/%i %s\n", bytes_read, required_size, cstr(path));
            Address elf_start = Kernel__alloc_kernel(required_size);
            Kernel__memcopy(addr(elf_data), elf_start, bytes_read);

            ResultMatch(ByteCount, Exec__read_remaining_elf(context, handle, elf_start, bytes_read, required_size))
                IsSuccess(total_read,
                {
                    Elf elf = Elf__new(elf_start, offset_addr(elf_start, total_read), path);
                    return Success(Elf, elf);
                })
                IsError(code, message, return Error(Elf, message, code))
            End
        });
        IsError(code, message,
        {
            if (code == IO_EOF)
            {
                __d_printf(PANIC, "EOF Reading ELF handle = %x\n", handle);
            }
            return Error(Elf, message, code);
        })
    End

    return Error(Elf, unreachable_code, SPAWN_FAILED);
}

Result(ProcessPtr) Exec__exec(OriginatingContext context, BString path, bool wait, ProcessArguments arguments)
{
    return Exec__exec_as(context, path, wait, None(UserID), arguments);
}

static Result(ProcessPtr) Exec__exec_as_checked(
    OriginatingContext context,
    BString path,
    bool wait,
    Optional(UserID) requested_user_id,
    ProcessArguments arguments,
    bool identity_authorized,
    SpawnStdio stdio
)
{
    __d_printf(EXEC, "Executing elf %s\n", cstr(path));

    if (!Exec__can_execute(context, &path))
    {
        return Error(ProcessPtr, permission_denied_message, SPAWN_FAILED);
    }

    Match(UserID, requested_user_id)
        IsSome(user_id,
        {
            if (!identity_authorized && !Exec__can_spawn_as(context, user_id))
            {
                return Error(ProcessPtr, permission_denied_message, SPAWN_FAILED);
            }
        })
    End

    ResultMatch(IOHandle, open(context, path, IO_OPEN_READ))
        IsError(code, message,
        {
            __d_printf(EXEC, "Failed to open elf %s\n", cstr(path));
            return Error(ProcessPtr, message, SPAWN_NO_FILE);
        })
        IsSuccess(handle,
        {
            __d_printf(EXEC, "Opened elf %s\n", cstr(path));

            ResultMatch(Elf, Exec__read_elf(context, handle, path))
                IsSuccess(elf,
                {
                    return Exec__launch(context, elf, wait, requested_user_id, arguments, stdio);
                })
                IsError(code, message,
                {
                    __d_printf(EXEC, "loading elf %s failed: %s\n", cstr(path), cstr(message));
                    return Error(ProcessPtr, message, SPAWN_FAILED);
                })
            End
        })
    End
    return Error(ProcessPtr, unreachable_code, SPAWN_FAILED);
}

Result(ProcessPtr) Exec__exec_as(
    OriginatingContext context,
    BString path,
    bool wait,
    Optional(UserID) requested_user_id,
    ProcessArguments arguments
)
{
    return Exec__exec_as_checked(context, path, wait, requested_user_id, arguments, false, SPAWN_STDIO_DEFAULT);
}

Result(ProcessPtr) Exec__exec_authorized_as(
    OriginatingContext context,
    BString path,
    bool wait,
    Optional(UserID) requested_user_id,
    ProcessArguments arguments
)
{
    return Exec__exec_as_checked(context, path, wait, requested_user_id, arguments, true, SPAWN_STDIO_DEFAULT);
}

Result(ProcessPtr) Exec__exec_authorized_as_with_stdio(
    OriginatingContext context,
    BString path,
    bool wait,
    Optional(UserID) requested_user_id,
    ProcessArguments arguments,
    SpawnStdio stdio
)
{
    return Exec__exec_as_checked(context, path, wait, requested_user_id, arguments, true, stdio);
}
