#include "../../../kernel/scheduler/scheduler.h"

static KString scheduler_not_linked = KSTRING_NEW("Scheduler not linked for this test");

Result(ProcessID) Scheduler__park_by_pid(
    SchedulerPtr self, ProcessID pid, KernelContextPtr kernel_context,
    Optional(Continuation) kernel_continuation, ParkReason reason
)
{
    return Error(ProcessID, scheduler_not_linked, SCHEDULER_PROCESS_NOT_FOUND);
}

Result(ProcessID) Scheduler__unpark_by_pid(
    SchedulerPtr self, ProcessID pid, ParkReason reason, Optional(ReturnCode) return_code
)
{
    return Error(ProcessID, scheduler_not_linked, SCHEDULER_PROCESS_NOT_FOUND);
}
