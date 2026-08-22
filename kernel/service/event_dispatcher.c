#include "event_dispatcher.h"

#include "../arch/init.h"
#include "../debug.h"
#include "../process.h"
#include "service.h"

MapBodyDefine(IRQServiceMap);
FixedArrayDefine(IRQNumber, 256, ServiceIRQNumbers, SimpleEquals(uint64_t));
FixedArrayBodyDefine(ServiceIRQNumbers);
FixedArrayBodyDefine(ServiceReadyNames);
FixedArrayBodyDefine(ServiceReadyWaiters);
FixedArrayBodyDefine(ServiceReadyProcesses);

static BString service_event_queue_empty = BSTRING_NEW("Service event queue is empty");
static BString service_ready_waiters_full = BSTRING_NEW("Service ready waiter queue is full");
static BString service_ready_names_full = BSTRING_NEW("Service ready name table is full");

bool ServiceReadyWaiter__equals(ServiceReadyWaiterPtr self, ServiceReadyWaiterPtr other)
{
    return self->process == other->process && BString__equals(&self->name, &other->name);
}

ServiceEventDispatcher ServiceEventDispatcher__new()
{
    return NEW_SERVICE_EVENT_DISPATCHER;
}

Result(ServiceCode) ServiceEventDispatcher__subscribe_irq(
    ServiceEventDispatcherPtr self, ProcessPtr process, IRQNumber irq
)
{
    __d_printf(IODEV, "service event dispatcher subscribe irq=%i pid=%i name=%s\n", irq, process->pid, cstr(process->name));
    IRQService service = (IRQService) {
        .process = process,
        .pending = false,
        .enabled = true,
        .delivery_count = 0,
        .coalesced_count = 0,
    };
    CriticalSection(self->lock,
    {
        IRQServiceMap__set(&self->irq_services, irq, service);
    })
    return Success(ServiceCode, SERVICE_OK);
}

Result(ServiceCode) ServiceEventDispatcher__enqueue(
    ServiceEventDispatcherPtr self, ProcessPtr process, ServiceEvent event
)
{
    __d_printf(
        IODEV,
        "service event dispatcher enqueue pid=%i name=%s type=%i source=%i data=%i queued=%i\n",
        process->pid,
        cstr(process->name),
        event.type,
        event.source,
        event.data,
        (int) ServiceEvents__len(&process->service.events)
    );

    CriticalSection(self->lock,
    {
        ResultMatch(ServiceCode, Service__push_next_event(process, event))
            IsError(code, message, _cs_return Error(ServiceCode, message, code))
        End
    })

    return Success(ServiceCode, SERVICE_OK);
}

Result(ServiceEventDispatch) ServiceEventDispatcher__dispatch_irq(
    ServiceEventDispatcherPtr self, InterruptControllerInfoPtr interrupt_controller, IRQNumber irq
)
{
    __d_printf(IODEV, "service event dispatcher irq=%i\n", irq);
    ServiceEventDispatch no_dispatch = (ServiceEventDispatch) {
        .process = None(ProcessPtr),
    };

    CriticalSection(self->lock,
    {
        MapPtrMatch(IRQServiceMap, IRQServiceMap__get_ptr(&self->irq_services, irq))
            IsSome(service,
            {
                ServiceEventDispatch dispatch = no_dispatch;
                dispatch.process.present = true;
                dispatch.process.some.val = service->process;

                __d_printf(
                    IODEV,
                    "service event dispatcher irq service irq=%i pid=%i pending=%i enabled=%i delivery=%i coalesced=%i\n",
                    irq,
                    service->process->pid,
                    (int) service->pending,
                    (int) service->enabled,
                    (int) service->delivery_count,
                    (int) service->coalesced_count
                );
                if (!service->enabled)
                {
                    _cs_return Success(ServiceEventDispatch, dispatch);
                }

                if (service->pending)
                {
                    service->coalesced_count++;
                    _cs_return Success(ServiceEventDispatch, dispatch);
                }

                service->pending = true;
                service->delivery_count++;
                Arch__mask_irq(interrupt_controller, irq);

                ServiceEvent event;
                event.type = SERVICE_EVENT_IRQ;
                event.source = irq;
                event.data = service->coalesced_count;

                ResultMatch(ServiceCode, Service__push_next_event(service->process, event))
                    IsError(code, message, _cs_return Error(ServiceEventDispatch, message, code))
                End

                _cs_return Success(ServiceEventDispatch, dispatch);
            })
        End
    })

    __d_printf(IODEV, "service event dispatcher no irq service irq=%i\n", irq);
    return Success(ServiceEventDispatch, no_dispatch);
}

static void ServiceEventDispatcher__mark_active_irq_event_locked(
    ServiceEventDispatcherPtr self, ProcessPtr process, ServiceEvent event
)
{
    if (event.type != SERVICE_EVENT_IRQ)
    {
        return;
    }

    MapPtrMatch(IRQServiceMap, IRQServiceMap__get_ptr(&self->irq_services, event.source))
        IsSome(service,
        {
            if (service->process == process && service->pending)
            {
                __d_printf(IODEV, "service event dispatcher active irq=%i pid=%i\n", event.source, process->pid);
                process->service.active_irq_event = Some(ServiceEvent, event);
            }
        })
    End
}

Result(ServiceEvent) ServiceEventDispatcher__take_next(ServiceEventDispatcherPtr self, ProcessPtr process)
{
    CriticalSection(self->lock,
    {
        ResultMatch(ServiceEvent, Service__pop_next_event(process))
            IsSuccess(event,
            {
                ServiceEventDispatcher__mark_active_irq_event_locked(self, process, event);
                _cs_return Success(ServiceEvent, event);
            })
            IsError(code, message, _cs_return Error(ServiceEvent, message, code))
        End
    })

    return Error(ServiceEvent, service_event_queue_empty, SERVICE_EVENT_QUEUE_EMPTY);
}

Result(ServiceCode) ServiceEventDispatcher__complete_active_irq(
    ServiceEventDispatcherPtr self, InterruptControllerInfoPtr interrupt_controller, ProcessPtr process
)
{
    CriticalSection(self->lock,
    {
        Match(ServiceEvent, process->service.active_irq_event)
            IsSome(event,
            {
                MapPtrMatch(IRQServiceMap, IRQServiceMap__get_ptr(&self->irq_services, event.source))
                    IsSome(service,
                    {
                        if (service->process == process && service->pending)
                        {
                            __d_printf(IODEV, "service event dispatcher complete active irq=%i pid=%i\n", event.source, process->pid);
                            service->pending = false;
                            service->coalesced_count = 0;
                            process->service.active_irq_event = None(ServiceEvent);
                            Arch__unmask_irq(interrupt_controller, event.source);
                            _cs_return Success(ServiceCode, SERVICE_OK);
                        }
                    })
                End
                process->service.active_irq_event = None(ServiceEvent);
            })
        End
    })

    return Success(ServiceCode, SERVICE_OK);
}

Result(ServiceCode) ServiceEventDispatcher__unsubscribe_process(
    ServiceEventDispatcherPtr self, ProcessPtr process
)
{
    ServiceIRQNumbers stale_irqs = ServiceIRQNumbers__new();

    CriticalSection(self->lock,
    {
        MapIterate(IRQServiceMap, self->irq_services, index, irq, service)
            if (service.process == process)
            {
                ResultMatch(CollectionCode, ServiceIRQNumbers__push(&stale_irqs, irq))
                    IsError(code, message, _cs_return Error(ServiceCode, message, SERVICE_EVENT_QUEUE_FULL))
                End
            }
        End

        FAIterate(ServiceIRQNumbers, stale_irqs, irq_index, irq)
            IRQServiceMap__delete(&self->irq_services, irq);
        End

        process->service.active_irq_event = None(ServiceEvent);
    })

    return ServiceEventDispatcher__remove_ready_waiter(self, process);
}

static bool ServiceReadyNames__matches(BStringPtr ready_name, void * context)
{
    ConstBStringPtr requested = context;
    return BString__equals(ready_name, requested);
}

static bool ServiceReadyWaiter__matches_process(ServiceReadyWaiterPtr waiter, void * context)
{
    ProcessPtr process = context;
    return waiter->process == process;
}

bool ServiceEventDispatcher__is_ready(ServiceEventDispatcherPtr self, ConstBStringPtr name)
{
    bool ready = false;

    CriticalSection(self->lock,
    {
        FindResultMatch(ServiceReadyNames, ServiceReadyNames__find(
            &self->ready_names, ServiceReadyNames__matches, (void *) name
        ))
            IsSome(match, ready = true)
        End
    })

    return ready;
}

Result(ServiceCode) ServiceEventDispatcher__publish_ready(
    ServiceEventDispatcherPtr self, ConstBStringPtr name, ServiceReadyProcessesPtr ready_processes
)
{
    CriticalSection(self->lock,
    {
        FindResultMatch(ServiceReadyNames, ServiceReadyNames__find(
            &self->ready_names, ServiceReadyNames__matches, (void *) name
        ))
            IsNone(
            {
                BString ready_name = BString__slice(name, 0, BString__length(name));
                ResultMatch(CollectionCode, ServiceReadyNames__push(&self->ready_names, ready_name))
                    IsError(code, message, _cs_return Error(ServiceCode, service_ready_names_full, SERVICE_EVENT_QUEUE_FULL))
                End
            })
        End

        ServiceReadyWaiters retained_waiters = ServiceReadyWaiters__new();
        FAIterate(ServiceReadyWaiters, self->ready_waiters, index, waiter)
            if (BString__equals(&waiter.name, name))
            {
                ResultMatch(CollectionCode, ServiceReadyProcesses__push(ready_processes, waiter.process))
                    IsError(code, message, _cs_return Error(ServiceCode, service_ready_waiters_full, SERVICE_EVENT_QUEUE_FULL))
                End
            }
            else
            {
                ResultMatch(CollectionCode, ServiceReadyWaiters__push(&retained_waiters, waiter))
                    IsError(code, message, _cs_return Error(ServiceCode, service_ready_waiters_full, SERVICE_EVENT_QUEUE_FULL))
                End
            }
        End
        self->ready_waiters = retained_waiters;
    })

    return Success(ServiceCode, SERVICE_OK);
}

Result(ServiceCode) ServiceEventDispatcher__wait_ready(
    ServiceEventDispatcherPtr self, ProcessPtr process, ConstBStringPtr name
)
{
    CriticalSection(self->lock,
    {
        FindResultMatch(ServiceReadyNames, ServiceReadyNames__find(
            &self->ready_names, ServiceReadyNames__matches, (void *) name
        ))
            IsSome(match, _cs_return Success(ServiceCode, SERVICE_OK))
        End

        ServiceReadyWaiter waiter;
        waiter.process = process;
        waiter.name = BString__slice(name, 0, BString__length(name));

        ResultMatch(CollectionCode, ServiceReadyWaiters__push(&self->ready_waiters, waiter))
            IsError(code, message, _cs_return Error(ServiceCode, service_ready_waiters_full, SERVICE_EVENT_QUEUE_FULL))
        End
    })

    return Success(ServiceCode, SERVICE_OK);
}

Result(ServiceCode) ServiceEventDispatcher__remove_ready_waiter(ServiceEventDispatcherPtr self, ProcessPtr process)
{
    CriticalSection(self->lock,
    {
        while (true)
        {
            FindResultMatch(ServiceReadyWaiters, ServiceReadyWaiters__find(
                &self->ready_waiters, ServiceReadyWaiter__matches_process, process
            ))
                IsSome(match,
                {
                    ResultMatch(CollectionCode, ServiceReadyWaiters__remove(&self->ready_waiters, *match.value))
                        IsError(code, message, _cs_return Error(ServiceCode, message, SERVICE_SCHEDULER_ERROR))
                    End
                })
                IsNone(_cs_return Success(ServiceCode, SERVICE_OK))
            End
        }
    })

    return Success(ServiceCode, SERVICE_OK);
}
