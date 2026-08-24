#include "process_handler.h"

#include "syscalls/process.h"
#include "../scheduler/kernel_context.h"
#include "../scheduler/scheduler.h"

static BString invalid_process_list_request = BSTRING_NEW("Invalid process list request");
static BString invalid_child_event_request = BSTRING_NEW("Invalid child event request");

static Result(Anything) ProcessChildEvent__copy_to_target(Result(Anything) result, Anything context)
{
    ProcessChildEventPtr target = (ProcessChildEventPtr) context;
    if (target == 0)
    {
        return Error(Anything, invalid_child_event_request, PROCESS_CHILD_EVENT_INVALID_ARGUMENT);
    }

    match_result(Anything, result,
        success(value)
        {
            ProcessChildEventPtr event = (ProcessChildEventPtr) value;
            *target = *event;
            return Success(Anything, PROCESS_CHILD_EVENT_OK);
        },
        error(code, message) { return Error(Anything, message, code); }
    );

    return Error(Anything, invalid_child_event_request, PROCESS_CHILD_EVENT_INVALID_ARGUMENT);
}

static Result(Anything) Process__complete_wait_child_event_syscall(ParkedOperationPtr parked_operation)
{
    match_optional(SchedulerPtr, parked_operation->kernel_context->scheduler,
        some(scheduler)
        {
            match_optional(ProcessID, parked_operation->kernel_context->pid,
                some(pid)
                {
                    match_optional(ProcessPtr, Scheduler__find_process_by_pid(scheduler, pid),
                        some(process)
                        {
                            match_result(ProcessChildEvent, Scheduler__child_take_next_event(scheduler, process),
                                success(event)
                                {
                                    return KernelContext__map_result(
                                        parked_operation->kernel_context,
                                        Success(Anything, (Anything) &event)
                                    );
                                },
                                error(code, message) { return Error(Anything, message, code); }
                            );
                        },
                        none_do_nothing()
                    );
                },
                none_do_nothing()
            );
        },
        none_do_nothing()
    );

    return Error(Anything, invalid_child_event_request, PROCESS_CHILD_EVENT_INVALID_ARGUMENT);
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

    return Success(SystemCallResult, Scheduler__process_info_snapshot(
        kernel->scheduler, request->entries, request->max_entries
    ));
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

    match_optional(ProcessPtr, proc,
        some(process)
        {
            ProcessChildEventPtr target = (ProcessChildEventPtr) arg1;
            KernelContextPtr kernel_context = KernelContext__heap_new_with_mapper(
                Some(ProcessID, process->pid),
                Some(SchedulerPtr, kernel->scheduler),
                ProcessChildEvent__copy_to_target,
                (Anything) target
            );

            match_result(ProcessChildEvent, Scheduler__child_wait_with_context_ownership(
                kernel->scheduler,
                process,
                kernel_context,
                Some(Continuation, Process__complete_wait_child_event_syscall),
                true
            ),
                success(event)
                {
                    Result(Anything) mapped = KernelContext__map_result(
                        kernel_context,
                        Success(Anything, (Anything) &event)
                    );
                    KernelContext__dispose_heap(kernel_context);
                    match_result(Anything, mapped,
                        success(mapped_code) { return Success(SystemCallResult, mapped_code); },
                        error(mapped_code, mapped_message) { return Error(SystemCallResult, mapped_message, mapped_code); }
                    );
                },
                error(code, message)
                {
                    if (code != PROCESS_CHILD_EVENT_QUEUE_EMPTY)
                    {
                        KernelContext__dispose_heap(kernel_context);
                    }
                    return Error(SystemCallResult, message, code);
                });
        },
        none_do_nothing()
    );

    return Error(SystemCallResult, invalid_child_event_request, PROCESS_CHILD_EVENT_INVALID_ARGUMENT);
}
