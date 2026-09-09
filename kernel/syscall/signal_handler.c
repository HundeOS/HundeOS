#include "signal_handler.h"

#include "../kernel_core.h"
#include "../scheduler/scheduler.h"
#include "syscalls/process.h"

Result(SystemCallResult) Signal__handler(KernelPtr kernel, OptionalPtr(ProcessPtr) proc, Anything arg1, Anything arg2,
                                         Anything arg3, Anything arg4, Anything arg5)
{
    ProcessID pid = (ProcessID)arg1;
    ProcessSignal signal = (ProcessSignal)arg2;

    match_result(
        Scheduler__queue_signal(kernel->scheduler, pid, signal),
        success(process_id) { return Success(SystemCallResult, PROCESS_SIGNAL_OK); },
        error(code, message) { return Error(SystemCallResult, message, code); }
    );
}
