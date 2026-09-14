#include "process_handler.h"

#include "syscalls/process.h"
#include "../scheduler/kernel_context.h"
#include "../scheduler/scheduler.h"

static BString invalid_process_list_request = bstr("Invalid process list request");
static BString invalid_child_event_request = bstr("Invalid child event request");
static BString invalid_sleep_request = bstr("Invalid sleep request");
static BString sleep_pending = bstr("Sleep pending");

Result(SystemCallResult) ProcessCredentials__handler(KernelPtr kernel, OptionalPtr(ProcessPtr) proc, Anything arg1,
                                                     Anything arg2, Anything arg3, Anything arg4, Anything arg5)
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
        error(code, message) { return Error(SystemCallResult, message, code); }
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
        error(code, message) { return Error(Anything, message, code); }
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
                parked_operation->kernel_context->pid,
                some(pid)
                {
                    match_optional(
                        Scheduler__find_process_by_pid(scheduler, pid),
                        some(process)
                        {
                            match_result(
                                Scheduler__child_take_next_event(scheduler, process),
                                success(event)
                                {
                                    Result(Anything) mapped_result = Success(Anything, (Anything)&event);
                                    return KernelContext__map_result(parked_operation->kernel_context, &mapped_result);
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

Result(SystemCallResult) ProcessList__handler(KernelPtr kernel, OptionalPtr(ProcessPtr) proc, Anything arg1,
                                              Anything arg2, Anything arg3, Anything arg4, Anything arg5)
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

Result(SystemCallResult) ProcessWaitChildEvent__handler(KernelPtr kernel, OptionalPtr(ProcessPtr) proc, Anything arg1,
                                                        Anything arg2, Anything arg3, Anything arg4, Anything arg5)
{
    if (arg1 == 0)
    {
        return Error(SystemCallResult, invalid_child_event_request, PROCESS_CHILD_EVENT_INVALID_ARGUMENT);
    }

    match_optional(
        *proc,
        some(process)
        {
            ProcessChildEventPtr target = (ProcessChildEventPtr)arg1;
            KernelContextPtr kernel_context = KernelContext__heap_new_with_mapper(
                &Some(ProcessID, process->pid), &Some(SchedulerPtr, kernel->scheduler),
                ProcessChildEvent__copy_to_target, (Anything)target);

            match_result(
                Scheduler__child_wait_with_context_ownership(
                    kernel->scheduler, process, kernel_context,
                    &Some(Continuation, Process__complete_wait_child_event_syscall), true),
                success(event)
                {
                    Result(Anything) mapped =
                        KernelContext__map_result(kernel_context, &Success(Anything, (Anything)&event));
                    KernelContext__dispose_heap(kernel_context);
                    match_result(
                        mapped,
                        success(mapped_code) { return Success(SystemCallResult, mapped_code); },
                        error(mapped_code, mapped_message)
                        {
                            return Error(SystemCallResult, mapped_message, mapped_code);
                        }
                    );
                },
                error(code, message)
                {
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

Result(SystemCallResult) ProcessSleep__handler(KernelPtr kernel, OptionalPtr(ProcessPtr) proc, Anything arg1,
                                               Anything arg2, Anything arg3, Anything arg4, Anything arg5)
{
    match_optional(
        *proc,
        some(process)
        {
            match_result(
                Scheduler__sleep_usec(kernel->scheduler, process, (uint64_t)arg1),
                success(sleeping) { return Error(SystemCallResult, sleep_pending, SYSCALL_IO_BLOCK); },
                error(code, message) { return Error(SystemCallResult, message, PROCESS_SLEEP_FAILED); }
            );
        },
        none_do_nothing()
    );

    return Error(SystemCallResult, invalid_sleep_request, PROCESS_SLEEP_INVALID_ARGUMENT);
}
