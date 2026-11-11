#include "process_handler.h"

#include "syscalls/process.h"
#include "../arch/cpu.h"
#include "../kalloc.h"
#include "../scheduler/kernel_context.h"
#include "../scheduler/scheduler.h"

static BString invalid_process_list_request = bstr("Invalid process list request");

Result(SystemCallResult)
    ThreadWait__handler(KernelPtr kernel, OptionalPtr(ProcessPtr) proc, Anything arg1, Anything arg2, Anything arg3,
                        Anything arg4, Anything arg5, OptionalPtr(ThreadPtr) caller_thread)
{
    if (arg2 > UINT32_MAX)
    {
        return Error(SystemCallResult, bstr("Invalid atomic wait value"), THREAD_WAIT_INVALID);
    }
    match_optional(
        *caller_thread,
        some(caller)
        {
            match_result(
                Scheduler__wait_atomic(kernel->scheduler, caller, arg1, (ThreadWaitValue)arg2, arg3),
                success(status)
                {
                    return Success(SystemCallResult, status);
                },
                error(code, message)
                {
                    return Error(SystemCallResult, message, code);
                }
            );
        },
        none_do_nothing()
    );
    return Error(SystemCallResult, bstr("Invalid atomic wait caller"), THREAD_WAIT_INVALID);
}

Result(SystemCallResult)
    ThreadWake__handler(KernelPtr kernel, OptionalPtr(ProcessPtr) proc, Anything arg1, Anything arg2, Anything arg3,
                        Anything arg4, Anything arg5, OptionalPtr(ThreadPtr) caller_thread)
{
    match_optional(
        *caller_thread,
        some(caller)
        {
            match_result(
                Scheduler__wake_atomic(kernel->scheduler, caller, arg1, arg2),
                success(count)
                {
                    return Success(SystemCallResult, count);
                },
                error(code, message)
                {
                    return Error(SystemCallResult, message, code);
                }
            );
        },
        none_do_nothing()
    );
    return Error(SystemCallResult, bstr("Invalid atomic wake caller"), THREAD_WAIT_INVALID);
}

static BString invalid_child_event_request = bstr("Invalid child event request");
static BString invalid_sleep_request = bstr("Invalid sleep request");
static BString invalid_idle_wait_request = bstr("Invalid idle wait request");
static BString invalid_process_times_request = bstr("Invalid process times request");
static BString invalid_process_identity_request = bstr("Invalid process identity request");
static BString sleep_pending = bstr("Sleep pending");
static BString invalid_fork_request = bstr("Invalid fork request");
static BString invalid_process_group_request = bstr("Invalid process group request");

Result(SystemCallResult)
    ThreadLocalImage__handler(KernelPtr kernel, OptionalPtr(ProcessPtr) proc, Anything arg1, Anything arg2,
                              Anything arg3, Anything arg4, Anything arg5, OptionalPtr(ThreadPtr) caller_thread)
{
    match_optional(
        *caller_thread,
        some(caller)
        {
            match_optional(
                Process__writable_user_kernel_mapping(caller->owner, arg1, sizeof(ThreadLocalImage)),
                some(mapping)
                {
                    Kernel__memcopy(addr(&caller->owner->image.thread_local_image), mapping.virtual_addr,
                                    sizeof(ThreadLocalImage));
                    return Success(SystemCallResult, THREAD_OPERATION_OK);
                },
                none_do_nothing()
            );
        },
        none_do_nothing()
    );
    return Error(SystemCallResult, bstr("Invalid thread-local request"), THREAD_OPERATION_INVALID);
}

Result(SystemCallResult)
    ThreadCreate__handler(KernelPtr kernel, OptionalPtr(ProcessPtr) proc, Anything arg1, Anything arg2, Anything arg3,
                          Anything arg4, Anything arg5, OptionalPtr(ThreadPtr) caller_thread)
{
    match_optional(
        *caller_thread,
        some(caller)
        {
            if (arg4 == 0 || arg4 % 16 != 0 ||
                is_nothing(Process__writable_user_kernel_mapping(caller->owner, arg4, 1)))
            {
                return Error(SystemCallResult, bstr("Invalid thread pointer"), THREAD_OPERATION_INVALID);
            }
            match_result(
                Scheduler__create_thread(kernel->scheduler, caller, arg1, arg2, arg3, arg4),
                success(id)
                {
                    return Success(SystemCallResult, id);
                },
                error(code, message)
                {
                    return Error(SystemCallResult, message, code);
                }
            );
        },
        none_do_nothing()
    );
    return Error(SystemCallResult, bstr("Invalid thread caller"), THREAD_OPERATION_INVALID);
}

Result(SystemCallResult)
    ThreadExit__handler(KernelPtr kernel, OptionalPtr(ProcessPtr) proc, Anything arg1, Anything arg2, Anything arg3,
                        Anything arg4, Anything arg5, OptionalPtr(ThreadPtr) caller_thread)
{
    match_optional(
        *caller_thread,
        some(caller)
        {
            match_result(
                Scheduler__exit_thread(kernel->scheduler, caller, arg1),
                success(_)
                {
                    return Success(SystemCallResult, THREAD_OPERATION_OK);
                },
                error(code, message)
                {
                    return Error(SystemCallResult, message, code);
                }
            );
        },
        none_do_nothing()
    );
    return Error(SystemCallResult, bstr("Invalid thread caller"), THREAD_OPERATION_INVALID);
}

Result(SystemCallResult)
    ThreadJoin__handler(KernelPtr kernel, OptionalPtr(ProcessPtr) proc, Anything arg1, Anything arg2, Anything arg3,
                        Anything arg4, Anything arg5, OptionalPtr(ThreadPtr) caller_thread)
{
    if (arg2 == 0)
    {
        return Error(SystemCallResult, bstr("Invalid thread join result"), THREAD_OPERATION_INVALID);
    }
    match_optional(
        *caller_thread,
        some(caller)
        {
            match_result(
                Scheduler__join_thread(kernel->scheduler, caller, (ThreadID)arg1, (ThreadJoinResultPtr)arg2),
                success(code)
                {
                    return Success(SystemCallResult, code);
                },
                error(code, message)
                {
                    return Error(SystemCallResult, message, code);
                }
            );
        },
        none_do_nothing()
    );
    return Error(SystemCallResult, bstr("Invalid thread caller"), THREAD_OPERATION_INVALID);
}

Result(SystemCallResult)
    ProcessRegistryMap__handler(KernelPtr kernel, OptionalPtr(ProcessPtr) proc, Anything arg1, Anything arg2,
                                Anything arg3, Anything arg4, Anything arg5, OptionalPtr(ThreadPtr) caller_thread)
{
    match_optional(
        *proc,
        some(process)
        {
            match_result(
                KernelProcessRegistry__map_process(&kernel->process_registry, process),
                success(_)
                {
                    return Success(SystemCallResult, PROCESS_REGISTRY_VADDR);
                },
                error(code, message)
                {
                    return Error(SystemCallResult, message, PROCESS_REGISTRY_MAP_FAILED);
                }
            );
        },
        none_do_nothing()
    );

    return Error(SystemCallResult, bstr("Invalid process registry caller"), PROCESS_REGISTRY_MAP_INVALID_CALLER);
}

Result(SystemCallResult)
    ProcessFork__handler(KernelPtr kernel, OptionalPtr(ProcessPtr) proc, Anything arg1, Anything arg2, Anything arg3,
                         Anything arg4, Anything arg5, OptionalPtr(ThreadPtr) caller_thread)
{
    match_optional(
        *proc,
        some(process)
        {
            switch ((ProcessForkOperation)arg1)
            {
            case PROCESS_FORK_PREPARE:
                match_result(
                    Process__fork(process),
                    success(child)
                    {
                        match_result(
                            Process__add_pending_fork_child(process, child),
                            success(_)
                            {
                                return Success(SystemCallResult, child->pid);
                            },
                            error(code, message)
                            {
                                Process__dispose_user_allocations(child);
                                Process__dispose_page_table(child);
                                Process__dispose(child);
                                return Error(SystemCallResult, message, PROCESS_FORK_NO_MEMORY);
                            }
                        );
                    },
                    error(code, message)
                    {
                        return Error(SystemCallResult, message, code);
                    }
                );
            case PROCESS_FORK_RESUME:
                match_result(
                    Process__take_pending_fork_child(process, (ProcessID)arg2),
                    success(child)
                    {
                        match_result(
                            Scheduler__schedule(kernel->scheduler, child),
                            success(_)
                            {
                                return Success(SystemCallResult, child->pid);
                            },
                            error(code, message)
                            {
                                Process__dispose_user_allocations(child);
                                Process__dispose_page_table(child);
                                Process__dispose(child);
                                return Error(SystemCallResult, message, PROCESS_FORK_SCHEDULE_FAILED);
                            }
                        );
                    },
                    error(code, message)
                    {
                        return Error(SystemCallResult, message, PROCESS_FORK_CHILD_NOT_FOUND);
                    }
                );
            case PROCESS_FORK_ABORT:
                match_result(
                    Process__abort_pending_fork(process, (ProcessID)arg2),
                    success(child_pid)
                    {
                        return Success(SystemCallResult, 0);
                    },
                    error(code, message)
                    {
                        return Error(SystemCallResult, message, PROCESS_FORK_CHILD_NOT_FOUND);
                    }
                );
            default:
            {
                return Error(SystemCallResult, bstr("Invalid fork operation"), PROCESS_FORK_INVALID_OPERATION);
            }
            }
        },
        none_do_nothing()
    );

    return Error(SystemCallResult, invalid_fork_request, PROCESS_FORK_INVALID_CALLER);
}

Result(SystemCallResult)
    ProcessCredentials__handler(KernelPtr kernel, OptionalPtr(ProcessPtr) proc, Anything arg1, Anything arg2,
                                Anything arg3, Anything arg4, Anything arg5, OptionalPtr(ThreadPtr) caller_thread)
{
    if (arg1 == 0 || arg2 == 0)
    {
        return Error(SystemCallResult, bstr("Invalid credential request"), PROCESS_CREDENTIALS_INVALID_ARGUMENT);
    }
    ProcessCredentialsPtr target = (ProcessCredentialsPtr)arg2;
    match_result(
        Scheduler__process_credentials(kernel->scheduler, (ProcessID)arg1),
        success(credentials)
        {
            *target = credentials;
            return Success(SystemCallResult, PROCESS_CREDENTIALS_OK);
        },
        error(code, message)
        {
            return Error(SystemCallResult, message, code);
        }
    );
}

static Result(Anything) ProcessChildEvent__copy_to_target(ResultPtr(Anything) result, Anything context)
{
    ProcessChildEventPtr target = (ProcessChildEventPtr)context;
    if (target == 0)
    {
        return Error(Anything, invalid_child_event_request, PROCESS_CHILD_EVENT_INVALID_ARGUMENT);
    }

    match_result(
        *result,
        success(value)
        {
            ProcessChildEventPtr event = (ProcessChildEventPtr)value;
            *target = *event;
            return Success(Anything, PROCESS_CHILD_EVENT_OK);
        },
        error(code, message)
        {
            return Error(Anything, message, code);
        }
    );

    return Error(Anything, invalid_child_event_request, PROCESS_CHILD_EVENT_INVALID_ARGUMENT);
}

static Result(Anything) Process__complete_wait_child_event_syscall(ParkedOperationPtr parked_operation)
{
    match_optional(
        parked_operation->kernel_context->scheduler,
        some(scheduler)
        {
            match_optional(
                parked_operation->kernel_context->thread,
                some(thread)
                {
                    match_result(
                        Scheduler__child_take_event(
                            scheduler, thread->owner,
                            (int64_t)unwrap_optional(parked_operation->operation_key, (OperationKey)-1),
                            parked_operation->operation_context),
                        success(event)
                        {
                            Result(Anything) mapped_result = Success(Anything, (Anything)&event);
                            return KernelContext__map_result(parked_operation->kernel_context, &mapped_result);
                        },
                        error(code, message)
                        {
                            return Error(Anything, message, code);
                        }
                    );
                },
                none_do_nothing()
            );
        },
        none_do_nothing()
    );

    return Error(Anything, invalid_child_event_request, PROCESS_CHILD_EVENT_INVALID_ARGUMENT);
}

Result(SystemCallResult)
    ProcessList__handler(KernelPtr kernel, OptionalPtr(ProcessPtr) proc, Anything arg1, Anything arg2, Anything arg3,
                         Anything arg4, Anything arg5, OptionalPtr(ThreadPtr) caller_thread)
{
    if (arg1 == 0)
    {
        return Error(SystemCallResult, invalid_process_list_request, PROCESS_LIST_INVALID_ARGUMENT);
    }

    ProcessListRequestPtr request = (ProcessListRequestPtr)arg1;
    if ((Anything)request->entries == 0)
    {
        return Error(SystemCallResult, invalid_process_list_request, PROCESS_LIST_INVALID_ARGUMENT);
    }

    return Success(SystemCallResult,
                   Scheduler__process_info_snapshot(kernel->scheduler, request->entries, request->max_entries));
}

Result(SystemCallResult)
    ProcessWaitChildEvent__handler(KernelPtr kernel, OptionalPtr(ProcessPtr) proc, Anything arg1, Anything arg2,
                                   Anything arg3, Anything arg4, Anything arg5, OptionalPtr(ThreadPtr) caller_thread)
{
    uint64_t options = (uint64_t)arg3;
    uint64_t supported_options = PROCESS_CHILD_WAIT_NOHANG | PROCESS_CHILD_WAIT_UNTRACED | PROCESS_CHILD_WAIT_CONTINUED;
    if (arg1 == 0 || (options & ~supported_options) != 0)
    {
        return Error(SystemCallResult, invalid_child_event_request, PROCESS_CHILD_EVENT_INVALID_ARGUMENT);
    }

    match_optional(
        *proc,
        some(process)
        {
            ProcessChildEventPtr target = (ProcessChildEventPtr)arg1;
            KernelContextPtr kernel_context =
                KernelContext__heap_new_with_mapper(caller_thread, &Some(SchedulerPtr, kernel->scheduler),
                                                    ProcessChildEvent__copy_to_target, (Anything)target);

            match_result(
                Scheduler__child_wait_with_context_ownership(
                    kernel->scheduler, process, kernel_context,
                    &Some(Continuation, Process__complete_wait_child_event_syscall), true, (int64_t)arg2, options),
                success(event)
                {
                    Result(Anything) mapped =
                        KernelContext__map_result(kernel_context, &Success(Anything, (Anything)&event));
                    KernelContext__dispose_heap(kernel_context);
                    match_result(
                        mapped,
                        success(mapped_code)
                        {
                            return Success(SystemCallResult, mapped_code);
                        },
                        error(mapped_code, mapped_message)
                        {
                            return Error(SystemCallResult, mapped_message, mapped_code);
                        }
                    );
                },
                error(code, message)
                {
                    if (code == PROCESS_CHILD_EVENT_PENDING)
                    {
                        KernelContext__dispose_heap(kernel_context);
                        return Success(SystemCallResult, PROCESS_CHILD_EVENT_PENDING);
                    }
                    if (code != PROCESS_CHILD_EVENT_QUEUE_EMPTY)
                    {
                        KernelContext__dispose_heap(kernel_context);
                    }
                    return Error(SystemCallResult, message, code);
                }
            );
        },
        none_do_nothing()
    );

    return Error(SystemCallResult, invalid_child_event_request, PROCESS_CHILD_EVENT_INVALID_ARGUMENT);
}

Result(SystemCallResult)
    ProcessSleep__handler(KernelPtr kernel, OptionalPtr(ProcessPtr) proc, Anything arg1, Anything arg2, Anything arg3,
                          Anything arg4, Anything arg5, OptionalPtr(ThreadPtr) caller_thread)
{
    match_optional(
        *proc,
        some(process)
        {
            match_optional(
                *caller_thread,
                some(thread)
                {
                    if (thread->owner != process)
                    {
                        return Error(SystemCallResult, invalid_sleep_request, PROCESS_SLEEP_INVALID_ARGUMENT);
                    }
                    match_result(
                        Scheduler__sleep_usec(kernel->scheduler, thread, (uint64_t)arg1),
                        success(sleeping)
                        {
                            return Error(SystemCallResult, sleep_pending, SYSCALL_IO_BLOCK);
                        },
                        error(code, message)
                        {
                            return Error(SystemCallResult, message, PROCESS_SLEEP_FAILED);
                        }
                    );
                },
                none_do_nothing()
            );
        },
        none_do_nothing()
    );

    return Error(SystemCallResult, invalid_sleep_request, PROCESS_SLEEP_INVALID_ARGUMENT);
}

Result(SystemCallResult)
    ProcessIdleWait__handler(KernelPtr kernel, OptionalPtr(ProcessPtr) proc, Anything arg1, Anything arg2,
                             Anything arg3, Anything arg4, Anything arg5, OptionalPtr(ThreadPtr) caller_thread)
{
    CPUID cpu = CPU__id();
    match_optional(
        *proc,
        some(process)
        {
            match_optional(
                Scheduler__idle_process(kernel->scheduler, cpu),
                some(idle_process)
                {
                    if (process == idle_process)
                    {
                        match_optional(
                            Scheduler__current_thread(kernel->scheduler, cpu),
                            some(thread)
                            {
                                if (thread->owner == process)
                                {
                                    Scheduler__yield_runnable(kernel->scheduler, thread, cpu);
                                }
                            },
                            none_do_nothing()
                        );
                        CPU__wait_for_interrupt();
                        return Success(SystemCallResult, PROCESS_IDLE_WAIT_OK);
                    }
                },
                none_do_nothing()
            );
        },
        none_do_nothing()
    );

    return Error(SystemCallResult, invalid_idle_wait_request, PROCESS_IDLE_WAIT_INVALID_CALLER);
}

Result(SystemCallResult)
    ProcessTimes__handler(KernelPtr kernel, OptionalPtr(ProcessPtr) proc, Anything arg1, Anything arg2, Anything arg3,
                          Anything arg4, Anything arg5, OptionalPtr(ThreadPtr) caller_thread)
{
    if (arg1 == 0)
    {
        return Error(SystemCallResult, invalid_process_times_request, PROCESS_TIMES_INVALID_ARGUMENT);
    }

    match_optional(
        *proc,
        some(process)
        {
            ProcessTimesPtr target = (ProcessTimesPtr)arg1;
            *target = Scheduler__process_times(kernel->scheduler, process);
            return Success(SystemCallResult, PROCESS_TIMES_OK);
        },
        none_do_nothing()
    );

    return Error(SystemCallResult, invalid_process_times_request, PROCESS_TIMES_INVALID_ARGUMENT);
}

Result(SystemCallResult)
    ProcessIdentity__handler(KernelPtr kernel, OptionalPtr(ProcessPtr) proc, Anything arg1, Anything arg2,
                             Anything arg3, Anything arg4, Anything arg5, OptionalPtr(ThreadPtr) caller_thread)
{
    if (arg1 == 0)
    {
        return Error(SystemCallResult, invalid_process_identity_request, PROCESS_IDENTITY_INVALID_ARGUMENT);
    }

    match_optional(
        *proc,
        some(process)
        {
            ProcessIdentityPtr target = (ProcessIdentityPtr)arg1;
            *target = ProcessIdentity__new(process->pid, unwrap_optional(process->parent_pid, 0));
            target->process_group_id = process->process_group_id;
            target->session_id = process->session_id;
            return Success(SystemCallResult, PROCESS_IDENTITY_OK);
        },
        none_do_nothing()
    );

    return Error(SystemCallResult, invalid_process_identity_request, PROCESS_IDENTITY_INVALID_ARGUMENT);
}

Result(SystemCallResult)
    ProcessGroup__handler(KernelPtr kernel, OptionalPtr(ProcessPtr) proc, Anything arg1, Anything arg2, Anything arg3,
                          Anything arg4, Anything arg5, OptionalPtr(ThreadPtr) caller_thread)
{
    if (arg1 == 0)
    {
        return Error(SystemCallResult, invalid_process_group_request, PROCESS_GROUP_INVALID_ARGUMENT);
    }
    ProcessGroupRequestPtr target = (ProcessGroupRequestPtr)arg1;
    ProcessGroupRequest request = *target;
    match_optional(
        *proc,
        some(process)
        {
            switch (request.operation)
            {
            case PROCESS_GROUP_GET:
                match_result(
                    Scheduler__process_group(kernel->scheduler, process, request.pid),
                    success(process_group_id)
                    {
                        target->result_id = (int64_t)process_group_id;
                        return Success(SystemCallResult, PROCESS_GROUP_OK);
                    },
                    error(code, message)
                    {
                        return Error(SystemCallResult, message, code);
                    }
                );
            case PROCESS_GROUP_SET:
                match_result(
                    Scheduler__set_process_group(kernel->scheduler, process, request.pid, request.process_group_id),
                    success(process_group_id)
                    {
                        target->result_id = (int64_t)process_group_id;
                        return Success(SystemCallResult, PROCESS_GROUP_OK);
                    },
                    error(code, message)
                    {
                        return Error(SystemCallResult, message, code);
                    }
                );
            case PROCESS_GROUP_CREATE_SESSION:
                match_result(
                    Scheduler__create_session(kernel->scheduler, process),
                    success(session_id)
                    {
                        target->result_id = (int64_t)session_id;
                        return Success(SystemCallResult, PROCESS_GROUP_OK);
                    },
                    error(code, message)
                    {
                        return Error(SystemCallResult, message, code);
                    }
                );
            default:
                return Error(SystemCallResult, invalid_process_group_request, PROCESS_GROUP_INVALID_ARGUMENT);
            }
        },
        none_do_nothing()
    );
    return Error(SystemCallResult, invalid_process_group_request, PROCESS_GROUP_INVALID_ARGUMENT);
}
