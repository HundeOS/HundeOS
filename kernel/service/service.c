#include "service.h"

#include "../process.h"
#include "../scheduler/scheduler.h"
#include "../scheduler/parked.h"
#include "../debug.h"

FixedArrayBodyDefine(ServiceEvents);

static KString not_service_process = KSTRING_NEW("Process is not a service process");
static KString event_queue_empty = KSTRING_NEW("Service event queue is empty");
static KString no_scheduler = KSTRING_NEW("No scheduler for service wait");
static KString no_process = KSTRING_NEW("No process for service wait");
static KString invalid_buffer = KSTRING_NEW("Invalid service event buffer");

bool ServiceEvent__equals(ServiceEventPtr self, ServiceEventPtr other)
{
    return self->type == other->type && self->source == other->source && self->data == other->data;
}

Result(ServiceEvent) Service__wait(ProcessPtr process, KernelContextPtr kernel_context)
{
    if (!ProcessClass__is_service(process->process_class))
    {
        return Error(ServiceEvent, not_service_process, SERVICE_NOT_SERVICE_PROCESS);
    }

    Match(ServiceEvent, ServiceEvents__pop_front(&process->service_events))
        IsSome(event, return Success(ServiceEvent, event))
    End

    Match(SchedulerPtr, kernel_context->scheduler)
        IsSome(scheduler,
        {
            ResultMatch(ProcessPtr, Scheduler__park(
                scheduler, process, kernel_context, None(Continuation), PARK_WAIT_SERVICE_EVENT
            ))
                IsError(code, message, return Error(ServiceEvent, message, code))
            End
        })
        IsNone(return Error(ServiceEvent, no_scheduler, SERVICE_SCHEDULER_ERROR))
    End

    return Error(ServiceEvent, event_queue_empty, SERVICE_EVENT_QUEUE_EMPTY);
}

static Result(ServiceCode) Service__copy_next_event(ProcessPtr process, ServiceEventPtr target)
{
    if (target == NULL)
    {
        return Error(ServiceCode, invalid_buffer, SERVICE_INVALID_BUFFER);
    }

    Match(ServiceEvent, ServiceEvents__pop_front(&process->service_events))
        IsSome(event,
        {
            *target = event;
            return Success(ServiceCode, SERVICE_OK);
        })
    End

    return Error(ServiceCode, event_queue_empty, SERVICE_EVENT_QUEUE_EMPTY);
}

Result(ServiceCode) Service__wait_to_buffer(ProcessPtr process, KernelContextPtr kernel_context, ServiceEventPtr target)
{
    if (!ProcessClass__is_service(process->process_class))
    {
        return Error(ServiceCode, not_service_process, SERVICE_NOT_SERVICE_PROCESS);
    }

    ResultMatch(ServiceCode, Service__copy_next_event(process, target))
        IsSuccess(code, return Success(ServiceCode, code))
        IsError(code, message,
        {
            if (code != SERVICE_EVENT_QUEUE_EMPTY)
            {
                return Error(ServiceCode, message, code);
            }
        })
    End

    process->service_wait_target = target;

    Match(SchedulerPtr, kernel_context->scheduler)
        IsSome(scheduler,
        {
            KernelContextPtr parked_kernel_context = KernelContext__heap_new(
                kernel_context->pid, kernel_context->scheduler
            );

            ResultMatch(ProcessPtr, Scheduler__park(
                scheduler, process, parked_kernel_context, Some(Continuation, Service__complete_wait), PARK_WAIT_SERVICE_EVENT
            ))
                IsError(code, message,
                {
                    process->service_wait_target = NULL;
                    return Error(ServiceCode, message, code);
                })
            End
        })
        IsNone(
        {
            process->service_wait_target = NULL;
            return Error(ServiceCode, no_scheduler, SERVICE_SCHEDULER_ERROR);
        })
    End

    return Error(ServiceCode, event_queue_empty, SERVICE_EVENT_QUEUE_EMPTY);
}

Result(Anything) Service__complete_wait(ParkedKernelContextPtr parked_context)
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
                            ServiceEventPtr target = process->service_wait_target;
                            process->service_wait_target = NULL;

                            ResultMatch(ServiceCode, Service__copy_next_event(process, target))
                                IsSuccess(code, return Success(Anything, code))
                                IsError(code, message, return Error(Anything, message, code))
                            End
                        })
                    End
                })
            End
        })
    End

    return Error(Anything, no_process, SERVICE_SCHEDULER_ERROR);
}

static bool Service__is_waiting_for_event(ProcessPtr process)
{
    Match(ParkedKernelContextPtr, Process__top_parked_context(process))
        IsSome(parked_context, return parked_context->park_reason == PARK_WAIT_SERVICE_EVENT)
    End

    return false;
}

Result(ServiceCode) Service__enqueue(SchedulerPtr scheduler, ProcessPtr process, ServiceEvent event)
{
    __d_printf(
        IODEV,
        "service enqueue pid=%i name=%s type=%i source=%i data=%i queued=%i\n",
        process->pid,
        cstr(process->name),
        event.type,
        event.source,
        event.data,
        (int) ServiceEvents__len(&process->service_events)
    );
    if (!ProcessClass__is_service(process->process_class))
    {
        return Error(ServiceCode, not_service_process, SERVICE_NOT_SERVICE_PROCESS);
    }

    ResultMatch(CollectionCode, ServiceEvents__push(&process->service_events, event))
        IsError(code, message, return Error(ServiceCode, message, SERVICE_EVENT_QUEUE_FULL))
    End

    if (Service__is_waiting_for_event(process))
    {
        __d_printf(IODEV, "service enqueue unparking pid=%i type=%i source=%i\n", process->pid, event.type, event.source);
        ResultMatch(ProcessPtr, Scheduler__unpark(scheduler, process, PARK_WAIT_SERVICE_EVENT, None(ReturnCode)))
            IsError(code, message, return Error(ServiceCode, message, SERVICE_SCHEDULER_ERROR))
        End
    }

    return Success(ServiceCode, SERVICE_OK);
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
            ResultMatch(ServiceCode, Service__enqueue(scheduler, process, event))
                IsError(code, message, return Error(ServiceCode, message, code))
            End
        }
    End

    return Success(ServiceCode, SERVICE_OK);
}
