#include "process_handler.h"

#include "syscalls/process.h"
#include "../scheduler/kernel_context.h"
#include "../scheduler/scheduler.h"

static BString invalid_process_list_request = BSTRING_NEW("Invalid process list request");
static BString invalid_child_event_request = BSTRING_NEW("Invalid child event request");
static BString process_user_root = BSTRING_NEW("root");
static BString process_user_hund = BSTRING_NEW("hund");
static BString process_user_user = BSTRING_NEW("user");
static BString process_user_unknown = BSTRING_NEW("unknown");

static Result(Anything) ProcessChildEvent__copy_to_target(Result(Anything) result, Anything context)
{
    ProcessChildEventPtr target = (ProcessChildEventPtr) context;
    if (target == 0)
    {
        return Error(Anything, invalid_child_event_request, PROCESS_CHILD_EVENT_INVALID_ARGUMENT);
    }

    ResultMatch(Anything, result)
        IsSuccess(value,
        {
            ProcessChildEventPtr event = (ProcessChildEventPtr) value;
            *target = *event;
            return Success(Anything, PROCESS_CHILD_EVENT_OK);
        })
        IsError(code, message, return Error(Anything, message, code))
    End

    return Error(Anything, invalid_child_event_request, PROCESS_CHILD_EVENT_INVALID_ARGUMENT);
}

static Result(Anything) Process__complete_wait_child_event_syscall(ParkedOperationPtr parked_operation)
{
    Match(SchedulerPtr, parked_operation->kernel_context->scheduler)
        IsSome(scheduler,
        {
            Match(ProcessID, parked_operation->kernel_context->pid)
                IsSome(pid,
                {
                    Match(ProcessPtr, Scheduler__find_process_by_pid(scheduler, pid))
                        IsSome(process,
                        {
                            ResultMatch(ProcessChildEvent, Scheduler__child_take_next_event(scheduler, process))
                                IsSuccess(event,
                                {
                                    return KernelContext__map_result(
                                        parked_operation->kernel_context,
                                        Success(Anything, (Anything) &event)
                                    );
                                })
                                IsError(code, message, return Error(Anything, message, code))
                            End
                        })
                    End
                })
            End
        })
    End

    return Error(Anything, invalid_child_event_request, PROCESS_CHILD_EVENT_INVALID_ARGUMENT);
}

static BString ProcessClass__snapshot_name(ProcessClass process_class)
{
    switch (process_class)
    {
        case PROCESS_CLASS_REALTIME_SERVICE:
            return BSTRING_NEW("rt-service");
        case PROCESS_CLASS_SERVICE:
            return BSTRING_NEW("service");
        case PROCESS_CLASS_NORMAL:
        default:
            return BSTRING_NEW("normal");
    }
}

static BString Process__snapshot_user_name(ProcessPtr process)
{
    if (process->user_id == PROCESS_USER_ROOT)
    {
        return process_user_root;
    }
    if (process->user_id == PROCESS_USER_HUND)
    {
        return process_user_hund;
    }
    if (process->user_id == PROCESS_USER_USER)
    {
        return process_user_user;
    }

    return process_user_unknown;
}

static ProcessInfo ProcessInfo__from_process(ProcessPtr process)
{
    ProcessInfo info = (ProcessInfo) {
        .pid = process->pid,
    };
    BString state = bstring(ProcessState__to_string(process->state));
    BString no_wait_reason = BSTRING_NEW("-");
    BString class_name = ProcessClass__snapshot_name(process->process_class);
    BString user_name = Process__snapshot_user_name(process);

    BString__copy_to(&process->name, info.name, PROCESS_INFO_NAME_SIZE);
    BString__copy_to(&state, info.state, PROCESS_INFO_STATE_SIZE);
    Match(ParkedOperationPtr, Process__top_parked_operation(process))
        IsSome(parked_operation,
        {
            BString wait_reason = ParkReason__to_string(parked_operation->park_reason);
            BString__copy_to(&wait_reason, info.wait_reason, PROCESS_INFO_WAIT_SIZE);
        })
        IsNone(BString__copy_to(&no_wait_reason, info.wait_reason, PROCESS_INFO_WAIT_SIZE))
    End
    BString__copy_to(&class_name, info.class_name, PROCESS_INFO_CLASS_SIZE);
    BString__copy_to(&user_name, info.user_name, PROCESS_INFO_USER_SIZE);
    return info;
}

Result(SystemCallResult) ProcessList__handler(
    KernelPtr kernel,
    Optional(ProcessPtr) proc,
    Anything arg1,
    Anything arg2,
    Anything arg3,
    Anything arg4,
    Anything arg5
)
{
    if (arg1 == 0)
    {
        return Error(SystemCallResult, invalid_process_list_request, PROCESS_LIST_INVALID_ARGUMENT);
    }

    ProcessListRequestPtr request = (ProcessListRequestPtr) arg1;
    if ((Anything) request->entries == 0)
    {
        return Error(SystemCallResult, invalid_process_list_request, PROCESS_LIST_INVALID_ARGUMENT);
    }

    ProcessTable processes = Scheduler__process_snapshot(kernel->scheduler);
    uint64_t written = 0;
    FAIterate(ProcessTable, processes, process_index, process)
        if (written >= request->max_entries)
        {
            return Success(SystemCallResult, written);
        }
        request->entries[written] = ProcessInfo__from_process(process);
        written++;
    End

    return Success(SystemCallResult, written);
}

Result(SystemCallResult) ProcessWaitChildEvent__handler(
    KernelPtr kernel,
    Optional(ProcessPtr) proc,
    Anything arg1,
    Anything arg2,
    Anything arg3,
    Anything arg4,
    Anything arg5
)
{
    if (arg1 == 0)
    {
        return Error(SystemCallResult, invalid_child_event_request, PROCESS_CHILD_EVENT_INVALID_ARGUMENT);
    }

    Match(ProcessPtr, proc)
        IsSome(process,
        {
            ProcessChildEventPtr target = (ProcessChildEventPtr) arg1;
            KernelContextPtr kernel_context = KernelContext__heap_new_with_mapper(
                Some(ProcessID, process->pid),
                Some(SchedulerPtr, kernel->scheduler),
                ProcessChildEvent__copy_to_target,
                (Anything) target
            );

            ResultMatch(ProcessChildEvent, Scheduler__child_wait_with_context_ownership(
                kernel->scheduler,
                process,
                kernel_context,
                Some(Continuation, Process__complete_wait_child_event_syscall),
                true
            ))
                IsSuccess(event,
                {
                    Result(Anything) mapped = KernelContext__map_result(
                        kernel_context,
                        Success(Anything, (Anything) &event)
                    );
                    KernelContext__dispose_heap(kernel_context);
                    ResultMatch(Anything, mapped)
                        IsSuccess(mapped_code, return Success(SystemCallResult, mapped_code))
                        IsError(mapped_code, mapped_message, return Error(SystemCallResult, mapped_message, mapped_code))
                    End
                })
                IsError(code, message,
                {
                    if (code != PROCESS_CHILD_EVENT_QUEUE_EMPTY)
                    {
                        KernelContext__dispose_heap(kernel_context);
                    }
                    return Error(SystemCallResult, message, code);
                })
            End
        })
    End

    return Error(SystemCallResult, invalid_child_event_request, PROCESS_CHILD_EVENT_INVALID_ARGUMENT);
}
