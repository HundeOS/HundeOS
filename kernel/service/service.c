#include "service.h"

#include "../process.h"
#include "../scheduler/scheduler.h"
#include "../scheduler/parked.h"
#include "../debug.h"

FixedArrayBodyDefine(ServiceEvents);

static BString not_service_process = bstr("Process is not a service process");
static BString event_queue_empty = bstr("Service event queue is empty");
static BString no_process = bstr("No process for service wait");
bool ServiceEvent__equals(ServiceEventPtr self, ServiceEventPtr other)
{
    return self->type == other->type && self->source == other->source && self->data == other->data;
}

ServiceState ServiceState__new()
{
    return (ServiceState){
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

    match_optional(
        ServiceEvents__pop_front(&process->service.events),
        some(event) { return Success(ServiceEvent, event); },
        none_do_nothing()
    );

    return Error(ServiceEvent, event_queue_empty, SERVICE_EVENT_QUEUE_EMPTY);
}

Result(ServiceCode) Service__push_next_event(ProcessPtr process, ServiceEvent event)
{
    if (!ProcessClass__is_service(process->process_class))
    {
        return Error(ServiceCode, not_service_process, SERVICE_NOT_SERVICE_PROCESS);
    }

    match_result(
        ServiceEvents__push(&process->service.events, event),
        success_do_nothing(),
        error(code, message) { return Error(ServiceCode, message, SERVICE_EVENT_QUEUE_FULL); }
    );

    return Success(ServiceCode, SERVICE_OK);
}

Result(ServiceEvent) Service__complete_wait(ParkedOperationPtr parked_operation)
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
                        some(process) { return Scheduler__service_take_next_event(scheduler, process); },
                        none_do_nothing()
                    );
                },
                none_do_nothing()
            );
        },
        none_do_nothing()
    );

    return Error(ServiceEvent, no_process, SERVICE_SCHEDULER_ERROR);
}
