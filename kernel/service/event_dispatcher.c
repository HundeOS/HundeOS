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

static BString service_event_queue_empty = bstr("Service event queue is empty");
static BString service_ready_waiters_full = bstr("Service ready waiter queue is full");
static BString service_ready_names_full = bstr("Service ready name table is full");

bool ServiceReadyWaiter__equals(ServiceReadyWaiterPtr self, ServiceReadyWaiterPtr other)
{
    return self->process == other->process && BString__equals(&self->name, &other->name);
}

bool ServiceReadyName__equals(ServiceReadyNamePtr self, ServiceReadyNamePtr other)
{
    return BString__equals(&self->name, &other->name);
}

static ServiceReadyName ServiceReadyName__new(ConstBStringPtr name, ProcessPtr process)
{
    return (ServiceReadyName){
        .name = BString__slice(name, 0, BString__length(name)),
        .process = process,
    };
}

static IRQService IRQService__new(ProcessPtr process)
{
    return (IRQService){
        .process = process,
        .pending = false,
        .enabled = true,
        .delivery_count = 0,
        .coalesced_count = 0,
    };
}

static ServiceEventDispatch ServiceEventDispatch__new(OptionalPtr(ProcessPtr) process)
{
    return (ServiceEventDispatch){
        .process = *process,
    };
}

ServiceEventDispatcher ServiceEventDispatcher__new()
{
    return NEW_SERVICE_EVENT_DISPATCHER;
}

Result(ServiceCode)
    ServiceEventDispatcher__subscribe_irq(ServiceEventDispatcherPtr self, ProcessPtr process, IRQNumber irq)
{
    __d_printf(IODEV, "service event dispatcher subscribe irq=%i pid=%i name=%s\n", irq, process->pid,
               cstr(process->name));
    IRQService service = IRQService__new(process);
    critical_section(self->lock, { IRQServiceMap__set(&self->irq_services, irq, service); });
    return Success(ServiceCode, SERVICE_OK);
}

Result(ServiceCode)
    ServiceEventDispatcher__enqueue(ServiceEventDispatcherPtr self, ProcessPtr process, ServiceEvent event)
{
    __d_printf(IODEV, "service event dispatcher enqueue pid=%i name=%s type=%i source=%i data=%i queued=%i\n",
               process->pid, cstr(process->name), event.type, event.source, event.data,
               (int)ServiceEvents__len(&process->service.events));

    critical_section(self->lock, {
        match_result(
            Service__push_next_event(process, event),
            success_do_nothing(),
            error(code, message) { cs_return Error(ServiceCode, message, code); }
        );
    });

    return Success(ServiceCode, SERVICE_OK);
}

Result(ServiceEventDispatch)
    ServiceEventDispatcher__dispatch_irq(ServiceEventDispatcherPtr self,
                                         InterruptControllerInfoPtr interrupt_controller, IRQNumber irq)
{
    __d_printf(IODEV, "service event dispatcher irq=%i\n", irq);
    ServiceEventDispatch no_dispatch = ServiceEventDispatch__new(&None(ProcessPtr));

    critical_section(self->lock, {
        match_optional(
            IRQServiceMap__get_ptr(&self->irq_services, irq),
            some(service)
            {
                ServiceEventDispatch dispatch = no_dispatch;
                dispatch.process.present = true;
                dispatch.process.some.val = service->process;

                __d_printf(
                    IODEV,
                    "service event dispatcher irq service irq=%i pid=%i pending=%i enabled=%i delivery=%i coalesced=%i\n",
                    irq, service->process->pid, (int)service->pending, (int)service->enabled,
                    (int)service->delivery_count, (int)service->coalesced_count);
                if (!service->enabled)
                {
                    cs_return Success(ServiceEventDispatch, dispatch);
                }

                if (service->pending)
                {
                    service->coalesced_count++;
                    cs_return Success(ServiceEventDispatch, dispatch);
                }

                service->pending = true;
                service->delivery_count++;
                Arch__mask_irq(interrupt_controller, irq);

                ServiceEvent event = ServiceEvent__new(SERVICE_EVENT_IRQ, irq, service->coalesced_count);

                match_result(
                    Service__push_next_event(service->process, event),
                    success_do_nothing(),
                    error(code, message) { cs_return Error(ServiceEventDispatch, message, code); }
                );

                cs_return Success(ServiceEventDispatch, dispatch);
            },
            none_do_nothing()
        );
    });

    __d_printf(IODEV, "service event dispatcher no irq service irq=%i\n", irq);
    return Success(ServiceEventDispatch, no_dispatch);
}

static void ServiceEventDispatcher__mark_active_irq_event_locked(ServiceEventDispatcherPtr self, ProcessPtr process,
                                                                 ServiceEvent event)
{
    if (event.type != SERVICE_EVENT_IRQ)
    {
        return;
    }

    match_optional(
        IRQServiceMap__get_ptr(&self->irq_services, event.source),
        some(service)
        {
            if (service->process == process && service->pending)
            {
                __d_printf(IODEV, "service event dispatcher active irq=%i pid=%i\n", event.source, process->pid);
                process->service.active_irq_event = Some(ServiceEvent, event);
            }
        },
        none_do_nothing()
    );
}

Result(ServiceEvent) ServiceEventDispatcher__take_next(ServiceEventDispatcherPtr self, ProcessPtr process)
{
    critical_section(self->lock, {
        match_result(
            Service__pop_next_event(process),
            success(event)
            {
                ServiceEventDispatcher__mark_active_irq_event_locked(self, process, event);
                cs_return Success(ServiceEvent, event);
            },
            error(code, message) { cs_return Error(ServiceEvent, message, code); }
        );
    });

    return Error(ServiceEvent, service_event_queue_empty, SERVICE_EVENT_QUEUE_EMPTY);
}

Result(ServiceCode)
    ServiceEventDispatcher__complete_active_irq(ServiceEventDispatcherPtr self,
                                                InterruptControllerInfoPtr interrupt_controller, ProcessPtr process)
{
    critical_section(self->lock, {
        match_optional(
            process->service.active_irq_event,
            some(event)
            {
                match_optional(
                    IRQServiceMap__get_ptr(&self->irq_services, event.source),
                    some(service)
                    {
                        if (service->process == process && service->pending)
                        {
                            __d_printf(IODEV, "service event dispatcher complete active irq=%i pid=%i\n", event.source,
                                       process->pid);
                            service->pending = false;
                            service->coalesced_count = 0;
                            process->service.active_irq_event = None(ServiceEvent);
                            Arch__unmask_irq(interrupt_controller, event.source);
                            cs_return Success(ServiceCode, SERVICE_OK);
                        }
                    },
                    none_do_nothing()
                );
                process->service.active_irq_event = None(ServiceEvent);
            },
            none_do_nothing()
        );
    });

    return Success(ServiceCode, SERVICE_OK);
}

Result(ServiceCode) ServiceEventDispatcher__unsubscribe_process(ServiceEventDispatcherPtr self, ProcessPtr process)
{
    ServiceIRQNumbers stale_irqs = ServiceIRQNumbers__new();

    critical_section(self->lock, {
        for_each_map(IRQServiceMap, self->irq_services, index, irq, service, {
            if (service.process == process)
            {
                match_result(
                    ServiceIRQNumbers__push(&stale_irqs, irq),
                    success_do_nothing(),
                    error(code, message) { cs_return Error(ServiceCode, message, SERVICE_EVENT_QUEUE_FULL); }
                );
            }
        });
        for_each_fa(ServiceIRQNumbers, stale_irqs, irq_index, irq,
                    { IRQServiceMap__delete(&self->irq_services, irq); });
        process->service.active_irq_event = None(ServiceEvent);
    });

    return ServiceEventDispatcher__remove_ready_waiter(self, process);
}

static bool ServiceReadyNames__matches(ServiceReadyNamePtr ready_name, void * context)
{
    ConstBStringPtr requested = context;
    return BString__equals(&ready_name->name, requested);
}

static bool ServiceReadyWaiter__matches_process(ServiceReadyWaiterPtr waiter, void * context)
{
    ProcessPtr process = context;
    return waiter->process == process;
}

bool ServiceEventDispatcher__is_ready(ServiceEventDispatcherPtr self, ConstBStringPtr name)
{
    bool ready = false;

    critical_section(self->lock, {
        match_find_result(
            ServiceReadyNames__find(&self->ready_names, ServiceReadyNames__matches, (void *)name),
            some(match) { ready = true; },
            none_do_nothing()
        );
    });

    return ready;
}

Optional(ProcessPtr) ServiceEventDispatcher__ready_process(ServiceEventDispatcherPtr self, ConstBStringPtr name)
{
    critical_section(self->lock, {
        match_find_result(
            ServiceReadyNames__find(&self->ready_names, ServiceReadyNames__matches, (void *)name),
            some(match) { cs_return Some(ProcessPtr, match.process); },
            none_do_nothing()
        );
    });

    return None(ProcessPtr);
}

Result(ServiceCode)
    ServiceEventDispatcher__publish_ready(ServiceEventDispatcherPtr self, ProcessPtr process, ConstBStringPtr name,
                                          ServiceReadyProcessesPtr ready_processes)
{
    critical_section(self->lock, {
        match_find_result(
            ServiceReadyNames__find(&self->ready_names, ServiceReadyNames__matches, (void *)name),
            some_do_nothing(),
            none()
            {
                ServiceReadyName ready_name = ServiceReadyName__new(name, process);
                match_result(
                    ServiceReadyNames__push(&self->ready_names, ready_name),
                    success_do_nothing(),
                    error(code, message)
                    {
                        cs_return Error(ServiceCode, service_ready_names_full, SERVICE_EVENT_QUEUE_FULL);
                    }
                );
            }
        );

        ServiceReadyWaiters retained_waiters = ServiceReadyWaiters__new();
        for_each_fa(ServiceReadyWaiters, self->ready_waiters, index, waiter, {
            if (BString__equals(&waiter.name, name))
            {
                match_result(
                    ServiceReadyProcesses__push(ready_processes, waiter.process),
                    success_do_nothing(),
                    error(code, message)
                    {
                        cs_return Error(ServiceCode, service_ready_waiters_full, SERVICE_EVENT_QUEUE_FULL);
                    }
                );
            }
            else
            {
                match_result(
                    ServiceReadyWaiters__push(&retained_waiters, waiter),
                    success_do_nothing(),
                    error(code, message)
                    {
                        cs_return Error(ServiceCode, service_ready_waiters_full, SERVICE_EVENT_QUEUE_FULL);
                    }
                );
            }
        });
        self->ready_waiters = retained_waiters;
    });

    return Success(ServiceCode, SERVICE_OK);
}

Result(ServiceCode)
    ServiceEventDispatcher__wait_ready(ServiceEventDispatcherPtr self, ProcessPtr process, ConstBStringPtr name)
{
    critical_section(self->lock, {
        match_find_result(
            ServiceReadyNames__find(&self->ready_names, ServiceReadyNames__matches, (void *)name),
            some(match) { cs_return Success(ServiceCode, SERVICE_OK); },
            none_do_nothing()
        );

        ServiceReadyWaiter waiter;
        waiter.process = process;
        waiter.name = BString__slice(name, 0, BString__length(name));

        match_result(
            ServiceReadyWaiters__push(&self->ready_waiters, waiter),
            success_do_nothing(),
            error(code, message)
            {
                cs_return Error(ServiceCode, service_ready_waiters_full, SERVICE_EVENT_QUEUE_FULL);
            }
        );
    });

    return Success(ServiceCode, SERVICE_OK);
}

Result(ServiceCode) ServiceEventDispatcher__remove_ready_waiter(ServiceEventDispatcherPtr self, ProcessPtr process)
{
    critical_section(self->lock, {
        while (true)
        {
            match_find_result(
                ServiceReadyWaiters__find(&self->ready_waiters, ServiceReadyWaiter__matches_process, process),
                some(waiter)
                {
                    match_result(
                        ServiceReadyWaiters__remove(&self->ready_waiters, waiter),
                        success_do_nothing(),
                        error(code, message) { cs_return Error(ServiceCode, message, SERVICE_SCHEDULER_ERROR); }
                    );
                },
                none() { cs_return Success(ServiceCode, SERVICE_OK); }
            );
        }
    });

    return Success(ServiceCode, SERVICE_OK);
}
