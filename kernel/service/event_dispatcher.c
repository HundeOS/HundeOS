#include "event_dispatcher.h"

#include "../arch/init.h"
#include "../debug.h"
#include "../process.h"
#include "service.h"

MapBodyDefine(IRQServiceMap);
FixedArrayDefine(IRQNumber, 256, ServiceIRQNumbers, SimpleEquals(uint64_t));
FixedArrayBodyDefine(ServiceIRQNumbers);

static BString service_event_queue_empty = BSTRING_NEW("Service event queue is empty");

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

Result(ServiceEventDispatch) ServiceEventDispatcher__dispatch_irq(ServiceEventDispatcherPtr self, IRQNumber irq)
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
                Arch__mask_irq(irq);

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
    ServiceEventDispatcherPtr self, ProcessPtr process
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
                            Arch__unmask_irq(event.source);
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

    return Success(ServiceCode, SERVICE_OK);
}
