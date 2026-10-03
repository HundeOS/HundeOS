#include "signal_handler.h"

#include "../kernel_core.h"
#include "../scheduler/scheduler.h"
#include "syscalls/process.h"

static BString invalid_signal_control = bstr("Invalid signal control request");

static Result(SystemCallResult) SignalControl__action(ProcessPtr process, ProcessSignalControlRequestPtr target,
                                                      ProcessSignalControlRequest request)
{
    if (!Process__supports_signal(process, request.signal))
    {
        return Error(SystemCallResult, invalid_signal_control, PROCESS_SIGNAL_INVALID_ARGUMENT);
    }

    request.previous_action = Process__signal_action(process, request.signal);
    if (request.replace != 0)
    {
        uint64_t supported_flags = PROCESS_SIGNAL_ACTION_RESTART | PROCESS_SIGNAL_ACTION_RESETHAND |
                                   PROCESS_SIGNAL_ACTION_NOCLDSTOP | PROCESS_SIGNAL_ACTION_NODEFER |
                                   PROCESS_SIGNAL_ACTION_NOCLDWAIT;
        if ((request.action.flags & ~supported_flags) != 0 ||
            (request.action.handler > PROCESS_SIGNAL_IGNORE_HANDLER && request.action.restorer == 0))
        {
            return Error(SystemCallResult, invalid_signal_control, PROCESS_SIGNAL_UNSUPPORTED);
        }
        match_result(
            Process__set_signal_action(process, request.signal, &request.action),
            success(_)
            {
                target->previous_action = request.previous_action;
                return Success(SystemCallResult, PROCESS_SIGNAL_OK);
            },
            error(code, message)
            {
                return Error(SystemCallResult, message, code);
            }
        );
    }

    target->previous_action = request.previous_action;
    return Success(SystemCallResult, PROCESS_SIGNAL_OK);
}

static Result(SystemCallResult)
    SignalControl__mask(ProcessPtr process, ProcessSignalControlRequestPtr target, ProcessSignalControlRequest request)
{
    request.previous_mask = Process__signal_mask(process);
    if (request.replace != 0)
    {
        match_result(
            Process__set_signal_mask(process, request.mask_operation, request.mask),
            success(_)
            {
                target->previous_mask = request.previous_mask;
                return Success(SystemCallResult, PROCESS_SIGNAL_OK);
            },
            error(code, message)
            {
                return Error(SystemCallResult, message, code);
            }
        );
    }

    target->previous_mask = request.previous_mask;
    return Success(SystemCallResult, PROCESS_SIGNAL_OK);
}

Result(SystemCallResult) Signal__handler(KernelPtr kernel, OptionalPtr(ProcessPtr) proc, Anything arg1, Anything arg2,
                                         Anything arg3, Anything arg4, Anything arg5)
{
    int64_t target = (int64_t)arg1;
    ProcessSignal signal = (ProcessSignal)arg2;

    match_optional(
        *proc,
        some(sender)
        {
            match_result(
                Scheduler__send_signal(kernel->scheduler, sender, target, signal),
                success(process_id)
                {
                    return Success(SystemCallResult, PROCESS_SIGNAL_OK);
                },
                error(code, message)
                {
                    return Error(SystemCallResult, message, code);
                }
            );
        },
        none_do_nothing()
    );

    return Error(SystemCallResult, invalid_signal_control, PROCESS_SIGNAL_INVALID_ARGUMENT);
}

Result(SystemCallResult) SignalControl__handler(KernelPtr kernel, OptionalPtr(ProcessPtr) proc, Anything arg1,
                                                Anything arg2, Anything arg3, Anything arg4, Anything arg5)
{
    if (arg1 == 0)
    {
        return Error(SystemCallResult, invalid_signal_control, PROCESS_SIGNAL_INVALID_ARGUMENT);
    }

    ProcessSignalControlRequestPtr target = (ProcessSignalControlRequestPtr)arg1;
    ProcessSignalControlRequest request = *target;
    match_optional(
        *proc,
        some(process)
        {
            switch (request.operation)
            {
            case PROCESS_SIGNAL_CONTROL_ACTION:
                return SignalControl__action(process, target, request);
            case PROCESS_SIGNAL_CONTROL_MASK:
                return SignalControl__mask(process, target, request);
            case PROCESS_SIGNAL_CONTROL_PENDING:
                target->previous_mask = Process__pending_signal_mask(process);
                return Success(SystemCallResult, PROCESS_SIGNAL_OK);
            case PROCESS_SIGNAL_CONTROL_SUSPEND:
                match_result(
                    Scheduler__signal_suspend(kernel->scheduler, process, request.mask),
                    success(suspended)
                    {
                        return Error(SystemCallResult, bstr("Signal suspend pending"), SYSCALL_IO_BLOCK);
                    },
                    error(code, message)
                    {
                        return Error(SystemCallResult, message, code);
                    }
                );
            case PROCESS_SIGNAL_CONTROL_ALARM:
                target->previous_alarm_seconds = Process__set_alarm(process, CPU__time_usec(), request.alarm_seconds);
                return Success(SystemCallResult, PROCESS_SIGNAL_OK);
            default:
                return Error(SystemCallResult, invalid_signal_control, PROCESS_SIGNAL_UNSUPPORTED);
            }
        },
        none_do_nothing()
    );

    return Error(SystemCallResult, invalid_signal_control, PROCESS_SIGNAL_INVALID_ARGUMENT);
}

Result(SystemCallResult) SignalReturn__handler(KernelPtr kernel, OptionalPtr(ProcessPtr) proc, Anything arg1,
                                               Anything arg2, Anything arg3, Anything arg4, Anything arg5)
{
    match_optional(
        *proc,
        some(process)
        {
            match_result(
                Process__restore_signal_context(process),
                success(_)
                {
                    return Success(SystemCallResult, SYSCALL_CONTEXT_RESTORED);
                },
                error(code, message)
                {
                    return Error(SystemCallResult, message, code);
                }
            );
        },
        none_do_nothing()
    );

    return Error(SystemCallResult, invalid_signal_control, PROCESS_SIGNAL_INVALID_ARGUMENT);
}
