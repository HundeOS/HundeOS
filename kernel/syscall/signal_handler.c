#include "signal_handler.h"

#include "../kernel_core.h"
#include "../scheduler/scheduler.h"
#include "syscalls/process.h"

static BString process_not_found = BSTRING_NEW("Process not found");
static BString unsupported_signal = BSTRING_NEW("Unsupported signal");
static BString invalid_signal_target = BSTRING_NEW("Invalid signal target");

Result(SystemCallResult) Signal__handler(
    KernelPtr kernel, Optional(ProcessPtr) proc, Anything arg1,
    Anything arg2, Anything arg3, Anything arg4, Anything arg5
)
{
    ProcessID pid = (ProcessID) arg1;
    ProcessSignal signal = (ProcessSignal) arg2;

    if (pid == 0)
    {
        return Error(SystemCallResult, invalid_signal_target, PROCESS_SIGNAL_INVALID_ARGUMENT);
    }

    match_optional(ProcessPtr, Scheduler__find_process_by_pid(kernel->scheduler, pid),
        some(target)
        {
            if (!Process__supports_signal(target, signal))
            {
                return Error(SystemCallResult, unsupported_signal, PROCESS_SIGNAL_UNSUPPORTED);
            }

            switch (target->state)
            {
                case PROC_STATE_RUNNING:
                case PROC_STATE_SWITCHING_CONTEXT:
                    Process__queue_signal(target, signal);
                    return Success(SystemCallResult, PROCESS_SIGNAL_OK);
                default:
                    Process__dispose_owned_kernel_resources(target);
                    match_result(ProcessPtr, Scheduler__exit(
                        kernel->scheduler, target, ProcessSignal__exit_code(signal)
                    ),
                        success(exited) { return Success(SystemCallResult, PROCESS_SIGNAL_OK); },
                        error(code, message) { return Error(SystemCallResult, message, code); }
                    );
                }
        },
        none() { return Error(SystemCallResult, process_not_found, PROCESS_SIGNAL_PROCESS_NOT_FOUND); }
    );

    return Error(SystemCallResult, process_not_found, PROCESS_SIGNAL_PROCESS_NOT_FOUND);
}
