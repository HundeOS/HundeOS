#include "exec_handler.h"

#include "../../common_includes/syscalls/spawn.h"
#include "../exec/exec.h"
#include "../exec/load.h"
#include "../scheduler/kernel_context.h"
#include "../scheduler/parked.h"
#include "../scheduler/scheduler.h"
#include "../types/bstring_additional_types.h"
#include "auth.h"

static BString exec_load_pending = bstr("Executable load pending");

typedef struct ValidatedExecPayload
{
    KernelAddress allocation;
    BString path;
} ValidatedExecPayload;

ResultDef(ValidatedExecPayload);

static ValidatedExecPayload ValidatedExecPayload__new(KernelAddress allocation, ConstBStringPtr path)
{
    return (ValidatedExecPayload){.allocation = allocation, .path = *path};
}

static Result(ValidatedExecPayload) ExecPayload__copy_from_user(ConstSpawnRequestPtr request)
{
    if (request->overlay_payload == 0 || request->overlay_payload_size < sizeof(ExecPayload))
    {
        return Error(ValidatedExecPayload, bstr("Invalid exec payload"), SPAWN_FAILED);
    }
    if (request->overlay_payload_size > EXEC_PAYLOAD_MAX)
    {
        return Error(ValidatedExecPayload, bstr("Exec payload too large"), SPAWN_ARG_TOO_LONG);
    }
    KernelAddress allocation = Kernel__alloc(request->overlay_payload_size);
    if (!allocation.valid)
    {
        return Error(ValidatedExecPayload, bstr("Cannot allocate exec payload"), SPAWN_NO_MEM);
    }
    Kernel__memcopy(request->overlay_payload, allocation.virtual_addr, request->overlay_payload_size);
    ConstExecPayloadPtr payload = (ConstExecPayloadPtr)allocation.virtual_addr;
    ExecPayloadStatus status = ExecPayload__validate(payload, request->overlay_payload_size);
    if (status != EXEC_PAYLOAD_OK)
    {
        Kernel__free(allocation.virtual_addr);
        return Error(ValidatedExecPayload, bstr("Invalid exec payload"),
                     status == EXEC_PAYLOAD_TOO_LARGE ? SPAWN_ARG_TOO_LONG : SPAWN_FAILED);
    }
    size_t path_length = ExecPayload__bounded_string_length(payload, payload->path_offset);
    if (path_length >= IO_PROTOCOL_DATA_SIZE)
    {
        Kernel__free(allocation.virtual_addr);
        return Error(ValidatedExecPayload, bstr("Exec path too long"), SPAWN_NO_PATH);
    }
    BString path = BString__from_chars(ExecPayload__path(payload), path_length);
    return Success(ValidatedExecPayload, ValidatedExecPayload__new(allocation, &path));
}

static Result(ProcessPtr) ExecLoad__caller(ParkedOperationPtr parked_operation)
{
    match_optional(
        parked_operation->kernel_context->scheduler,
        some(scheduler)
        {
            match_optional(
                parked_operation->kernel_context->pid,
                some(pid)
                {
                    match_optional(
                        Scheduler__find_process_by_pid(scheduler, pid),
                        some(process)
                        {
                            return Success(ProcessPtr, process);
                        },
                        none_do_nothing()
                    );
                },
                none_do_nothing()
            );
        },
        none_do_nothing()
    );
    return Error(ProcessPtr, bstr("Executable load caller unavailable"), SPAWN_FAILED);
}

static Result(Anything) ExecLoad__complete_syscall(ParkedOperationPtr parked_operation)
{
    match_result(
        ExecLoad__caller(parked_operation),
        success(process)
        {
            match_result(
                ExecLoad__continue(process),
                success(child)
                {
                    return Success(Anything, child == process ? process->startup_argument_count : child->pid);
                },
                error(code, message)
                {
                    if (code != SPAWN_PENDING)
                    {
                        return Error(Anything, message, code);
                    }
                    SchedulerPtr scheduler = process->kernel->scheduler;
                    match_result(
                        Scheduler__park_with_context_ownership(scheduler, process, parked_operation->kernel_context,
                                                               &Some(Continuation, ExecLoad__complete_syscall),
                                                               PARK_WAIT_EXEC_LOAD, true),
                        success(parked)
                        {
                            parked_operation->owns_kernel_context = false;
                            return Success(Anything, 0);
                        },
                        error(park_code, park_message)
                        {
                            return Error(Anything, park_message, park_code);
                        }
                    );
                }
            );
        },
        error(code, message)
        {
            return Error(Anything, message, code);
        }
    );
    return Error(Anything, bstr("Executable load continuation failed"), SPAWN_FAILED);
}

static Result(SystemCallResult)
    ExecLoad__start_syscall(ProcessPtr process, ConstOriginatingContextPtr context, ConstBStringPtr path, bool wait,
                            OptionalPtr(UserID) user, ConstProcessArgumentsPtr arguments, ConstSpawnStdioPtr stdio,
                            OptionalPtr(KernelAddress) abort_report, bool overlay, KernelAddress overlay_payload)
{
    match_result(
        ExecLoad__begin(context, path, wait, user, arguments, stdio, abort_report, overlay, overlay_payload),
        success(child)
        {
            return Success(SystemCallResult, child->pid);
        },
        error(code, message)
        {
            if (code != SPAWN_PENDING)
            {
                if (overlay_payload.valid)
                {
                    Kernel__free(overlay_payload.virtual_addr);
                }
                return Error(SystemCallResult, message, code);
            }
        }
    );
    match_result(
        ExecLoad__continue(process),
        success(child)
        {
            return Success(SystemCallResult, child == process ? process->startup_argument_count : child->pid);
        },
        error(code, message)
        {
            if (code != SPAWN_PENDING)
            {
                return Error(SystemCallResult, message, code);
            }
        }
    );
    KernelContextPtr kernel_context = KernelContext__from_originating_context_heap(context);
    match_result(
        Scheduler__park_with_context_ownership(process->kernel->scheduler, process, kernel_context,
                                               &Some(Continuation, ExecLoad__complete_syscall), PARK_WAIT_EXEC_LOAD,
                                               true),
        success(parked)
        {
            return Error(SystemCallResult, exec_load_pending, SYSCALL_IO_BLOCK);
        },
        error(code, message)
        {
            KernelContext__dispose_heap(kernel_context);
            ExecLoad__cancel(process);
            return Error(SystemCallResult, message, code);
        }
    );
    return Error(SystemCallResult, exec_load_pending, SPAWN_FAILED);
}

Result(SystemCallResult) Exec__handler(KernelPtr kernel, OptionalPtr(ProcessPtr) proc, Anything arg1, Anything arg2,
                                       Anything arg3, Anything arg4, Anything arg5)
{
    return ExecRequest__handler(kernel, proc, arg1, arg2, arg3, arg4, arg5);
}

static Result(SystemCallResult)
    ExecRequest__execute(KernelPtr kernel, OptionalPtr(ProcessPtr) proc, Anything arg1, Anything arg2, Anything arg3,
                         Anything arg4, Anything arg5, bool bootstrap)
{
    if (arg1 == 0)
    {
        return Error(SystemCallResult, bstring("Invalid exec request"), SPAWN_FAILED);
    }

    ProcessPtr process;
    match_optional(
        *proc,
        some(value)
        {
            process = value;
            if (!is_nothing(process->exec_load))
            {
                return Error(SystemCallResult, bstr("Executable load already pending"), SPAWN_FAILED);
            }
        },
        none()
        {
            return Error(SystemCallResult, bstr("No process"), AUTH_NO_PROCESS);
        }
    );

    SpawnRequest request = *(SpawnRequestPtr)arg1;

    bool overlay = request.mode == P_OVERLAY;
    if (overlay && (bootstrap || request.auth_ticket_id != SPAWN_INHERIT_AUTH_TICKET))
    {
        return Error(SystemCallResult, bstr("Invalid process overlay request"), SPAWN_FAILED);
    }
    KernelAddress overlay_payload = NULL_KERNEL_ADDRESS;
    BString requested_path = request.path;
    if (overlay)
    {
        match_result(
            ExecPayload__copy_from_user(&request),
            success(validated)
            {
                overlay_payload = validated.allocation;
                requested_path = validated.path;
            },
            error(code, message)
            {
                return Error(SystemCallResult, message, code);
            }
        );
    }

    ProcessArguments arguments = ProcessArguments__new();
    if (!overlay)
    {
        SpawnArguments requested_arguments = request.arguments;
        for_each_fa(
            SpawnArguments,
            requested_arguments,
            argument_index,
            argument,
            {
                match_result(
                    ProcessArguments__push(&arguments, argument),
                    success_do_nothing(),
                    error(code, message)
                    {
                        return Error(SystemCallResult, message, SPAWN_ARG_TOO_LONG);
                    }
                );
            }
        );
    }

    CallContext call_context = CallContext__new(proc, SYSCALL_EXEC_REQUEST, arg1, arg2, arg3, arg4, arg5);
    OriginatingContext originating_context = OriginatingContext__syscall(kernel, &call_context);

    SpawnMode mode = request.mode;
    bool wait = mode == P_WAIT;
    Optional(KernelAddress) abort_report = None(KernelAddress);
    if (request.abort_report != 0)
    {
        if (!wait)
        {
            return Error(SystemCallResult, bstr("Abort report requires a synchronous spawn"), SPAWN_FAILED);
        }
        match_optional(
            Process__writable_user_kernel_mapping(process, request.abort_report, sizeof(SpawnAbortReport)),
            some(address)
            {
                abort_report = Some(KernelAddress, address);
                *(SpawnAbortReportPtr)address.virtual_addr = SpawnAbortReport__none();
            },
            none()
            {
                return Error(SystemCallResult, bstr("Invalid abort report mapping"), SPAWN_FAILED);
            }
        );
    }
    Optional(UserID) requested_user_id = None(UserID);
    if (request.auth_ticket_id != SPAWN_INHERIT_AUTH_TICKET)
    {
        match_optional(
            *proc,
            some(process)
            {
                match_result(
                    Auth__consume_ticket(&kernel->auth, process, request.auth_ticket_id),
                    success(user_id)
                    {
                        requested_user_id = Some(UserID, user_id);
                        ;
                    },
                    error(code, message)
                    {
                        return Error(SystemCallResult, message, code);
                    }
                );
            },
            none()
            {
                return Error(SystemCallResult, bstring("No process"), AUTH_NO_PROCESS);
            }
        );
    }

    if (!bootstrap)
    {
        return ExecLoad__start_syscall(process, &originating_context, &requested_path, wait, &requested_user_id,
                                       &arguments, &request.stdio, &abort_report, overlay, overlay_payload);
    }
    Result(ProcessPtr) exec_res = Exec__exec_authorized_as_with_stdio(&originating_context, &requested_path, wait,
                                                                      &requested_user_id, &arguments, &request.stdio);
    match_result(
        exec_res,
        success(spawned_proc)
        {
            return Success(SystemCallResult, spawned_proc->pid);
        },
        error(code, message)
        {
            return Error(SystemCallResult, message, code);
        }
    );

    return Error(SystemCallResult, bstring("Exec request handler failed"), SPAWN_FAILED);
}

Result(SystemCallResult) ExecRequest__handler(KernelPtr kernel, OptionalPtr(ProcessPtr) proc, Anything arg1,
                                              Anything arg2, Anything arg3, Anything arg4, Anything arg5)
{
    return ExecRequest__execute(kernel, proc, arg1, arg2, arg3, arg4, arg5, false);
}

static bool Boot__authorized(KernelPtr kernel, OptionalPtr(ProcessPtr) proc)
{
    match_optional(
        *proc,
        some(process)
        {
            return kernel->boot_authority != 0 && kernel->boot_authority == process->pid &&
                   !__atomic_load_n(&kernel->boot_complete, __ATOMIC_ACQUIRE);
        },
        none()
        {
            return false;
        }
    );
}

Result(SystemCallResult) BootSpawn__handler(KernelPtr kernel, OptionalPtr(ProcessPtr) proc, Anything arg1,
                                            Anything arg2, Anything arg3, Anything arg4, Anything arg5)
{
    if (!Boot__authorized(kernel, proc))
    {
        return Error(SystemCallResult, bstr("Bootstrap authority unavailable"), SPAWN_FAILED);
    }

    return ExecRequest__execute(kernel, proc, arg1, arg2, arg3, arg4, arg5, true);
}

Result(SystemCallResult) BootComplete__handler(KernelPtr kernel, OptionalPtr(ProcessPtr) proc, Anything arg1,
                                               Anything arg2, Anything arg3, Anything arg4, Anything arg5)
{
    if (!Boot__authorized(kernel, proc))
    {
        return Error(SystemCallResult, bstr("Bootstrap authority unavailable"), SPAWN_FAILED);
    }

    SchedulerPtr scheduler = kernel->scheduler;
    critical_section(
        scheduler->scheduler_lock,
        {
            ServiceDirectoryPtr directory = KernelServiceDirectory__kernel_directory(&kernel->service_directory);
            if (is_nothing(ServiceDirectory__find_endpoint(directory, &bstr(IO_SYSTEM_SERVICE))) ||
                is_nothing(ServiceDirectory__find_endpoint(directory, &bstr(IO_FILESYSTEM_SERVICE))) ||
                is_nothing(ServiceDirectory__find_endpoint(directory, &bstr(CHANNEL_WARDEN_SERVICE_NAME))) ||
                is_nothing(ServiceDirectory__find_endpoint(directory, &bstr(BUFFER_SERVICE_NAME))) ||
                is_nothing(ServiceDirectory__find_endpoint(directory, &bstr("/sbin"))) ||
                is_nothing(ServiceDirectory__find_endpoint(directory, &bstr("/boot"))) ||
                is_nothing(ServiceDirectory__find_endpoint(directory, &bstr("/boot/config"))))
            {
                cs_return Error(SystemCallResult, bstr("Bootstrap services unavailable"), SPAWN_FAILED);
            }
            __atomic_store_n(&kernel->boot_complete, true, __ATOMIC_RELEASE);
        }
    );
    return Success(SystemCallResult, SPAWN_SUCCESS);
}
