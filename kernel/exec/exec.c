#include "exec.h"

#include "../../common_includes/syscalls/io.h"
#include "../../common_includes/syscalls/spawn.h"
#include "../types/result.h"
#include "../bin/elf.h"
#include "../bin/embedded_binaries.h"
#include "../kalloc.h"
#include "../syscall/deferred.h"

static BString unreachable_code = bstr("unreachable code");
static BString permission_denied_message = bstr("Permission denied");
static const BString drivers_prefix = bstr("/sbin/drivers/");
static const BString services_prefix = bstr("/sbin/services/");
static const BString hundeschule_program = bstr("/sbin/services/hundeschule");
static const BString hundeschule_boot_program = bstr("services/hundeschule");
static const BString hundeschau_program = bstr("/sbin/services/hundeschau");
static const BString hundeschau_boot_program = bstr("services/hundeschau");
static const BString hundekanal_warter_program = bstr("/sbin/services/hundekanal-warter");
static const BString hundebus_program = bstr("/sbin/drivers/hundebus");
static const BString sdhci_program = bstr("/sbin/drivers/sdhci_channelio");
static const BString uart_rx_program = bstr("/sbin/drivers/uart_rx");
static const BString uart_tx_program = bstr("/sbin/drivers/uart_tx");

static bool Exec__is_hund_owned_program(ConstBStringPtr path)
{
    return BString__equals(path, &bstr("services/hunde-mama")) ||
           BString__equals(path, &bstr("/sbin/services/hunde-mama")) || BString__equals(path, &hundeschau_program) ||
           BString__equals(path, &hundeschule_program) || BString__equals(path, &hundeschule_boot_program) ||
           BString__equals(path, &hundeschau_boot_program) || BString__equals(path, &hundekanal_warter_program) ||
           BString__equals(path, &hundebus_program) || BString__equals(path, &sdhci_program) ||
           BString__equals(path, &uart_rx_program) || BString__equals(path, &uart_tx_program);
}

void Exec__apply_builtin_identity_policy(ProcessPtr self)
{
    if (Exec__is_hund_owned_program(&self->name))
    {
        Process__set_hund_identity(self);
    }
}

bool Exec__can_execute(ConstOriginatingContextPtr context, ConstBStringPtr path)
{
    if (!BString__starts_with(path, &drivers_prefix) && !BString__starts_with(path, &services_prefix))
    {
        return true;
    }

    match_optional(
        context->process,
        some(process)
        {
            return process->group_id == PROCESS_GROUP_HUND;
        },
        none_do_nothing()
    );

    return true;
}

static bool Exec__can_spawn_as(ConstOriginatingContextPtr context, UserID requested_user_id)
{
    match_optional(
        context->process,
        some(process)
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
        },
        none_do_nothing()
    );

    return false;
}

static void Exec__apply_requested_identity(ProcessPtr self, OptionalPtr(UserID) requested_user_id)
{
    match_optional(
        *requested_user_id,
        some(user_id)
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
        },
        none_do_nothing()
    );
}

Result(ProcessPtr) Exec__launch_loaded_elf(ConstOriginatingContextPtr context, ElfPtr elf, bool wait,
                                           OptionalPtr(UserID) requested_user_id, ConstProcessArgumentsPtr arguments,
                                           ConstSpawnStdioPtr stdio, bool overlay, KernelAddress overlay_payload)
{
    match_result(
        Elf__validate(elf),
        success_do_nothing(),
        error(code, message)
        {
            Kernel__free(elf->start);
            return Error(ProcessPtr, message, SPAWN_FAILED);
        }
    );
    if (overlay)
    {
        match_optional(
            context->process,
            some(process)
            {
                match_result(
                    Process__setup_overlay_candidate(process, elf, (ConstExecPayloadPtr)overlay_payload.virtual_addr),
                    success(candidate)
                    {
                        Kernel__free(elf->start);
                        match_result(
                            Process__commit_overlay(process, candidate),
                            success(_)
                            {
                                return Success(ProcessPtr, process);
                            },
                            error(code, message)
                            {
                                return Error(ProcessPtr, message, code);
                            }
                        );
                    },
                    error(code, message)
                    {
                        Kernel__free(elf->start);
                        return Error(ProcessPtr, message, code);
                    }
                );
            },
            none()
            {
                Kernel__free(elf->start);
                return Error(ProcessPtr, bstr("No process to overlay"), SPAWN_FAILED);
            }
        );
    }
    SpawnStdio startup = SPAWN_STDIO_DEFAULT;
    match_optional(
        context->process,
        some(parent)
        {
            match_result(
                Process__stdio_for_spawn(parent, stdio),
                success(value)
                {
                    startup = value;
                },
                error(code, message)
                {
                    Kernel__free(elf->start);
                    return Error(ProcessPtr, message, SPAWN_FAILED);
                }
            );
        },
        none()
        {
            if (SpawnStdio__is_present(*stdio))
            {
                Kernel__free(elf->start);
                return Error(ProcessPtr, bstr("No parent process for stdio"), SPAWN_FAILED);
            }
        }
    );
    __d_printf(EXEC, "Launching elf %s\n", cstr(elf->name));
    KernelPtr kernel = context->kernel;
    Optional(ProcessPtr) parent = context->process;
    Result(ProcessPtr) proc = Process__setup_with_args(kernel, elf, &parent, arguments);
    Kernel__free(elf->start);
    match_result(
        proc,
        success(process)
        {
            ProcessPtr new_proc = process;
            Exec__apply_requested_identity(new_proc, requested_user_id);
            Exec__apply_builtin_identity_policy(new_proc);
            new_proc->startup_stdio = startup;
            match_optional(
                context->process,
                some(parent)
                {
                    __d_printf(EXEC, "Parent %p\n", parent);
                    Process__prepare_startup_stdio(new_proc);
                    match_result(
                        Scheduler__schedule(kernel->scheduler, new_proc),
                        success_do_nothing(),
                        error(code, message)
                        {
                            return Error(ProcessPtr, message, SPAWN_FAILED);
                        }
                    );

                    if (wait)
                    {
                        __d_printf(EXEC, "Parking parent pid=%i name=%s waiting_for_pid=%i name=%s\n", parent->pid,
                                   cstr(parent->name), new_proc->pid, cstr(new_proc->name));
                        // TODO we need a continuation to give the result of the proc back to the parent
                        KernelContextPtr kernel_context = KernelContext__from_originating_context_heap(context);

                        Scheduler__park_with_context_ownership(kernel->scheduler, parent, kernel_context,
                                                               &Some(Continuation, SystemCall__Complete),
                                                               PARK_WAIT_CHILD, true);
                        __d_printf(EXEC, "Parent parked pid=%i child_pid=%i\n", parent->pid, new_proc->pid);
                    }
                    __d_printf(EXEC, "Scheduling elf %s\n", cstr(elf->name));

                    return Success(ProcessPtr, new_proc);
                },
                none_do_nothing()
            );
        },
        error(code, message)
        {
            return Error(ProcessPtr, message, SPAWN_FAILED);
        }
    );
    return Error(ProcessPtr, unreachable_code, SPAWN_FAILED);
}

Result(ProcessPtr)
    Exec__exec(ConstOriginatingContextPtr context, ConstBStringPtr path, bool wait, ConstProcessArgumentsPtr arguments)
{
    Optional(UserID) requested_user_id = None(UserID);
    return Exec__exec_as(context, path, wait, &requested_user_id, arguments);
}

static Result(ProcessPtr)
    Exec__exec_as_checked(ConstOriginatingContextPtr context, ConstBStringPtr path, bool wait,
                          OptionalPtr(UserID) requested_user_id, ConstProcessArgumentsPtr arguments,
                          bool identity_authorized, ConstSpawnStdioPtr stdio)
{
    __d_printf(EXEC, "Executing elf %s\n", cstr(*path));

    if (!Exec__can_execute(context, path))
    {
        return Error(ProcessPtr, permission_denied_message, SPAWN_FAILED);
    }

    match_optional(
        *requested_user_id,
        some(user_id)
        {
            if (!identity_authorized && !Exec__can_spawn_as(context, user_id))
            {
                return Error(ProcessPtr, permission_denied_message, SPAWN_FAILED);
            }
        },
        none_do_nothing()
    );

    match_optional(
        context->process,
        some(process)
        {
            if (__atomic_load_n(&context->kernel->boot_complete, __ATOMIC_ACQUIRE) ||
                context->kernel->boot_authority != process->pid || context->kernel->boot_authority == 0)
            {
                return Error(ProcessPtr, bstr("Bootstrap execution unavailable"), SPAWN_FAILED);
            }
        },
        none()
        {
            return Error(ProcessPtr, bstr("No bootstrap authority"), SPAWN_FAILED);
        }
    );

    // Bootstrap execution reads the immutable archive, not a kernel filesystem.
    if (BString__starts_with(path, &bstr("/sbin/")))
    {
        BString relative = BString__slice(path, 6, BString__length(path));
        match_optional(
            EmbeddedElf__load(&relative),
            some(embedded)
            {
                size_t size = embedded.end - embedded.start;
                Address copy = Kernel__alloc_kernel(size);
                Kernel__memcopy(embedded.start, copy, size);
                Elf elf = Elf__new(copy, copy + size, path);
                return Exec__launch_loaded_elf(context, &elf, wait, requested_user_id, arguments, stdio, false,
                                               NULL_KERNEL_ADDRESS);
            },
            none()
            {
                return Error(ProcessPtr, bstr("Embedded executable not found"), SPAWN_NO_FILE);
            }
        );
    }
    return Error(ProcessPtr, bstr("Bootstrap executable must be embedded"), SPAWN_NO_FILE);
}

Result(ProcessPtr) Exec__exec_as(ConstOriginatingContextPtr context, ConstBStringPtr path, bool wait,
                                 OptionalPtr(UserID) requested_user_id, ConstProcessArgumentsPtr arguments)
{
    SpawnStdio stdio = SPAWN_STDIO_DEFAULT;
    return Exec__exec_as_checked(context, path, wait, requested_user_id, arguments, false, &stdio);
}

Result(ProcessPtr) Exec__exec_authorized_as(ConstOriginatingContextPtr context, ConstBStringPtr path, bool wait,
                                            OptionalPtr(UserID) requested_user_id, ConstProcessArgumentsPtr arguments)
{
    SpawnStdio stdio = SPAWN_STDIO_DEFAULT;
    return Exec__exec_as_checked(context, path, wait, requested_user_id, arguments, true, &stdio);
}

Result(ProcessPtr) Exec__exec_authorized_as_with_stdio(ConstOriginatingContextPtr context, ConstBStringPtr path,
                                                       bool wait, OptionalPtr(UserID) requested_user_id,
                                                       ConstProcessArgumentsPtr arguments, ConstSpawnStdioPtr stdio)
{
    return Exec__exec_as_checked(context, path, wait, requested_user_id, arguments, true, stdio);
}
