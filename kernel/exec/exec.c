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
static const BString drivers_prefix = BSTRING_NEW("/sbin/drivers/");
static const BString hundeschau_program = BSTRING_NEW("/sbin/hundeschau");
static const BString hundeschau_boot_program = BSTRING_NEW("hundeschau");
static const BString virtio_blk_program = BSTRING_NEW("/sbin/drivers/virtio_blk");
static const BString uart_rx_program = BSTRING_NEW("/sbin/drivers/uart_rx");
static const BString uart_tx_program = BSTRING_NEW("/sbin/drivers/uart_tx");

static bool Exec__is_hund_owned_program(ConstBStringPtr path)
{
    return BString__equals(path, &hundeschau_program)
        || BString__equals(path, &hundeschau_boot_program)
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
    ProcessArguments arguments
)
{
    __d_printf(EXEC, "Launching elf %s\n", cstr(elf.name));
    KernelPtr kernel = context.kernel;
    Result(ProcessPtr) proc = Process__setup_with_args(elf, context.process, arguments);
    ResultMatch(ProcessPtr, proc)
        IsSuccess(process,
        {
            ProcessPtr new_proc = process;
            Exec__apply_requested_identity(new_proc, requested_user_id);
            Exec__apply_builtin_identity_policy(new_proc);
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

static Result(Elf) Exec__read_elf(OriginatingContext context, IOHandle handle, BString path)
{
    DataBufferPtr buffer = DataBuffer__heap_new();
    __d_println(EXEC, "Reading elf");
    ResultMatch(ByteCount, read(context, handle, buffer))
        IsSuccess(bytes_read,
        {
            __d_printf(EXEC, "Copying elf %i %s\n", bytes_read, cstr(path));
            Address elf_start = Kernel__alloc_kernel(bytes_read);
            Kernel__memcopy(addr(&buffer->data), elf_start, bytes_read);

            Elf elf = Elf__new(elf_start, offset_addr(elf_start, bytes_read), path);
            return Success(Elf, elf);
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

Result(ProcessPtr) Exec__exec_as(
    OriginatingContext context,
    BString path,
    bool wait,
    Optional(UserID) requested_user_id,
    ProcessArguments arguments
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
            if (!Exec__can_spawn_as(context, user_id))
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
                    return Exec__launch(context, elf, wait, requested_user_id, arguments);
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
