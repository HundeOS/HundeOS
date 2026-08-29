#include "../../../kernel/scheduler/scheduler.h"

static BString scheduler_not_linked = bstr("Scheduler not linked for this test");

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

Result(ServiceEvent) Scheduler__service_wait(
    SchedulerPtr self, ProcessPtr process, KernelContextPtr kernel_context, Optional(Continuation) continuation
)
{
    return Error(ServiceEvent, scheduler_not_linked, SCHEDULER_PROCESS_NOT_FOUND);
}

Result(ServiceCode) Scheduler__service_enqueue(SchedulerPtr self, ProcessPtr process, ServiceEvent event)
{
    return Success(ServiceCode, SERVICE_OK);
}
