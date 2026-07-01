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

Result(ServiceEvent) Service__pop_next_event(ProcessPtr process)
{
    if (!ProcessClass__is_service(process->process_class))
    {
        return Error(ServiceEvent, not_service_process, SERVICE_NOT_SERVICE_PROCESS);
    }

    Match(ServiceEvent, ServiceEvents__pop_front(&process->service_events))
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

    ResultMatch(CollectionCode, ServiceEvents__push(&process->service_events, event))
        IsError(code, message, return Error(ServiceCode, message, SERVICE_EVENT_QUEUE_FULL))
    End

    return Success(ServiceCode, SERVICE_OK);
}

Result(ServiceEvent) Service__complete_wait(ParkedKernelContextPtr parked_context)
{
    Match(SchedulerPtr, parked_context->kernel_context->scheduler)
        IsSome(scheduler,
        {
            Match(ProcessID, parked_context->kernel_context->pid)
                IsSome(pid,
                {
                    Match(ProcessPtr, Scheduler__find_process_by_pid(scheduler, pid))
                        IsSome(process,
                        {
                            return Service__pop_next_event(process);
                        })
                    End
                })
            End
        })
    End

    return Error(ServiceEvent, no_process, SERVICE_SCHEDULER_ERROR);
}

Result(ServiceCode) Service__send_timer_tick_events(SchedulerPtr scheduler, uint64_t tick_count)
{
    if (tick_count == 0 || tick_count % SERVICE_TIMER_EVENT_INTERVAL != 0)
    {
        return Success(ServiceCode, SERVICE_OK);
    }

    ServiceEvent event = (ServiceEvent) {
        .type = SERVICE_EVENT_IRQ,
        .source = SERVICE_TIMER_TICK_SOURCE,
        .data = tick_count,
    };

    ProcessTable processes = Scheduler__all_processes_for_all_cpus(scheduler);
    FAIterate(ProcessTable, processes, index, process)
        if (ProcessClass__is_service(process->process_class))
        {
            ResultMatch(ServiceCode, Scheduler__service_enqueue(scheduler, process, event))
                IsError(code, message, return Error(ServiceCode, message, code))
            End
        }
    End

    return Success(ServiceCode, SERVICE_OK);
}
