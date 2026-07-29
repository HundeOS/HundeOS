#include "service.h"

#include "../process.h"
#include "../scheduler/scheduler.h"
#include "../scheduler/parked.h"
#include "../debug.h"

FixedArrayBodyDefine(ServiceEvents);

static KString not_service_process = KSTRING_NEW("Process is not a service process");
static KString event_queue_empty = KSTRING_NEW("Service event queue is empty");
static KString no_process = KSTRING_NEW("No process for service wait");
bool ServiceEvent__equals(ServiceEventPtr self, ServiceEventPtr other)
{
    return self->type == other->type && self->source == other->source && self->data == other->data;
}

ServiceState ServiceState__new()
{
    return (ServiceState) {
        .events = ServiceEvents__new(),
        .active_irq_event = None(ServiceEvent),
    };
}

Result(ServiceEvent) Service__pop_next_event(ProcessPtr process)
{
    if (!ProcessClass__is_service(process->process_class))
    {
        return Error(ServiceEvent, not_service_process, SERVICE_NOT_SERVICE_PROCESS);
    }

    Match(ServiceEvent, ServiceEvents__pop_front(&process->service.events))
        IsSome(event, return Success(ServiceEvent, event))
    End

    return Error(ServiceEvent, event_queue_empty, SERVICE_EVENT_QUEUE_EMPTY);
}

Result(ServiceCode) Service__push_next_event(ProcessPtr process, ServiceEvent event)
{
    if (!ProcessClass__is_service(process->process_class))
    {
        return Error(ServiceCode, not_service_process, SERVICE_NOT_SERVICE_PROCESS);
    }

    ResultMatch(CollectionCode, ServiceEvents__push(&process->service.events, event))
        IsError(code, message, return Error(ServiceCode, message, SERVICE_EVENT_QUEUE_FULL))
    End

    return Success(ServiceCode, SERVICE_OK);
}

Result(ServiceEvent) Service__complete_wait(ParkedOperationPtr parked_operation)
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
                            return Scheduler__service_take_next_event(scheduler, process);
                        })
                    End
                })
            End
        })
    End

    return Error(ServiceEvent, no_process, SERVICE_SCHEDULER_ERROR);
}
