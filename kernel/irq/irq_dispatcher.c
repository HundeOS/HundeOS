#include "irq_dispatcher.h"

#include "../arch/page_table.h"
#include "../kernel_core.h"
#include "../../common/ipc/ipc_platform.h"

static BString missing_dispatcher = bstr("Missing IRQ dispatcher");
static BString missing_kernel_consumer = bstr("Missing IRQ kernel consumer");
static BString missing_service_process = bstr("Missing IRQ service process");
static BString missing_scheduler = bstr("Missing scheduler for IRQ service consumer");
static BString missing_interrupt_controller = bstr("Missing interrupt controller info");
static BString irq_already_registered = bstr("IRQ already registered");
static BString interrupt_manager_exists = bstr("Interrupt manager already registered");
static BString interrupt_manager_required = bstr("Interrupt manager is not registered");

MapBodyDefine(IRQConsumerMap);
FixedArrayDefine(IRQNumber, 256, IRQNumbers, SimpleEquals(uint64_t));
FixedArrayBodyDefine(IRQNumbers);

static IRQConsumer IRQConsumer__new(IRQConsumerType type, IRQKernelConsumer kernel_consumer, void * kernel_data,
                                    ProcessPtr service_process)
{
    return (IRQConsumer){
        .type = type,
        .kernel_consumer = kernel_consumer,
        .kernel_data = kernel_data,
        .service_process = service_process,
        .delivery_sequence = 0,
    };
}

Result(IRQRegistrationStatus)
    IRQDispatcher__register_kernel(IRQDispatcherPtr self, IRQNumber irq, IRQKernelConsumer consumer, void * data)
{
    if (self == NULL) // hundelint: ignore-no-null
    {
        return Error(IRQRegistrationStatus, missing_dispatcher, IRQ_REGISTRATION_ERROR);
    }
    if (consumer == NULL) // hundelint: ignore-no-null
    {
        return Error(IRQRegistrationStatus, missing_kernel_consumer, IRQ_INVALID_CONSUMER);
    }

    IRQConsumer irq_consumer = IRQConsumer__new(IRQ_CONSUMER_KERNEL, consumer, data,
                                                NULL // hundelint: ignore-no-null
    );

    critical_section(self->lock, {
        match_optional(
            IRQConsumerMap__get_ptr(&self->consumers, irq),
            some(existing) { cs_return Error(IRQRegistrationStatus, irq_already_registered, IRQ_ALREADY_REGISTERED); },
            none_do_nothing()
        );
        IRQConsumerMap__set(&self->consumers, irq, irq_consumer);
    });

    return Success(IRQRegistrationStatus, IRQ_REGISTERED);
}

Result(IRQRegistrationStatus) IRQDispatcher__register_service(IRQDispatcherPtr self, IRQNumber irq, ProcessPtr process)
{
    if (self == NULL) // hundelint: ignore-no-null
    {
        return Error(IRQRegistrationStatus, missing_dispatcher, IRQ_REGISTRATION_ERROR);
    }
    if (process == NULL) // hundelint: ignore-no-null
    {
        return Error(IRQRegistrationStatus, missing_service_process, IRQ_INVALID_CONSUMER);
    }
    if (self->scheduler == NULL) // hundelint: ignore-no-null
    {
        return Error(IRQRegistrationStatus, missing_scheduler, IRQ_REGISTRATION_ERROR);
    }
    if (is_nothing(self->interrupt_controller))
    {
        return Error(IRQRegistrationStatus, missing_interrupt_controller, IRQ_REGISTRATION_ERROR);
    }

    InterruptControllerInfoPtr interrupt_controller = &self->interrupt_controller.some.val;

    IRQConsumer irq_consumer = IRQConsumer__new(IRQ_CONSUMER_SERVICE,
                                                NULL, // hundelint: ignore-no-null
                                                NULL, // hundelint: ignore-no-null
                                                process);

    critical_section(self->lock, {
        match_optional(
            IRQConsumerMap__get_ptr(&self->consumers, irq),
            some(existing) { cs_return Error(IRQRegistrationStatus, irq_already_registered, IRQ_ALREADY_REGISTERED); },
            none_do_nothing()
        );
        IRQConsumerMap__set(&self->consumers, irq, irq_consumer);
    });
    __d_printf(IODEV, "irq register service irq=%i pid=%i name=%s\n", irq, process->pid, cstr(process->name));

    match_result(
        Scheduler__subscribe_irq(self->scheduler, process, irq),
        success_do_nothing(),
        error(code, message) { return Error(IRQRegistrationStatus, message, IRQ_REGISTRATION_ERROR); }
    );

    match_result(
        Arch__enable_irq(interrupt_controller, irq),
        success_do_nothing(),
        error(code, message) { return Error(IRQRegistrationStatus, message, IRQ_REGISTRATION_ERROR); }
    );

    return Success(IRQRegistrationStatus, IRQ_REGISTERED);
}

Result(Address) IRQDispatcher__register_manager(IRQDispatcherPtr self, ProcessPtr process)
{
    critical_section(self->lock, {
        if (!is_nothing(self->manager))
        {
            cs_return Error(Address, interrupt_manager_exists, SERVICE_INTERRUPT_MANAGER_EXISTS);
        }
    });

    match_result(
        Kernel__try_alloc_user(KERNEL_NOTIFICATION_QUEUE_ADDRESS, KERNEL_NOTIFICATION_QUEUE_PAGE_SIZE, true, true,
                               false),
        success(memory)
        {
            KernelNotificationQueuePtr queue = k_v_ptr(memory, KernelNotificationQueue);
            *queue = KernelNotificationQueue__new();
            PageTable__map_shared(&process->page_table, process->kernel, memory, KERNEL_NOTIFICATION_QUEUE_PAGE_SIZE);
            critical_section(self->lock, {
                if (!is_nothing(self->manager))
                {
                    PageTable__unmap(&process->page_table, process->kernel, KERNEL_NOTIFICATION_QUEUE_ADDRESS,
                                     KERNEL_NOTIFICATION_QUEUE_PAGE_SIZE);
                    Kernel__free(memory.kernel_addr.virtual_addr);
                    cs_return Error(Address, interrupt_manager_exists, SERVICE_INTERRUPT_MANAGER_EXISTS);
                }
                self->manager = Some(ProcessPtr, process);
                self->manager_queue_memory = Some(UserAddress, memory);
            });
            return Success(Address, KERNEL_NOTIFICATION_QUEUE_ADDRESS);
        },
        error(code, message) { return Error(Address, message, SERVICE_MAP_FAILED); }
    );
    return Error(Address, interrupt_manager_required, SERVICE_MAP_FAILED);
}

Result(IRQRegistrationStatus)
    IRQDispatcher__register_managed_irq(IRQDispatcherPtr self, IRQNumber irq, ProcessPtr manager)
{
    if (irq >= IRQ_COUNT)
    {
        return Error(IRQRegistrationStatus, missing_interrupt_controller, IRQ_REGISTRATION_ERROR);
    }
    if (is_nothing(self->interrupt_controller))
    {
        return Error(IRQRegistrationStatus, missing_interrupt_controller, IRQ_REGISTRATION_ERROR);
    }

    critical_section(self->lock, {
        match_optional(
            self->manager,
            some(registered_manager)
            {
                if (registered_manager != manager)
                {
                    cs_return Error(IRQRegistrationStatus, interrupt_manager_required, IRQ_INVALID_CONSUMER);
                }
            },
            none() { cs_return Error(IRQRegistrationStatus, interrupt_manager_required, IRQ_INVALID_CONSUMER); }
        );
        match_optional(
            IRQConsumerMap__get_ptr(&self->consumers, irq),
            some(existing) { cs_return Error(IRQRegistrationStatus, irq_already_registered, IRQ_ALREADY_REGISTERED); },
            none_do_nothing()
        );
        IRQConsumerMap__set(&self->consumers, irq,
                            IRQConsumer__new(IRQ_CONSUMER_MANAGER,
                             NULL, // hundelint: ignore-no-null
                             NULL, // hundelint: ignore-no-null
                                             manager));
    });

    match_optional(
        self->interrupt_controller,
        some(controller)
        {
            match_result(
                Arch__enable_irq(&controller, irq),
                success_do_nothing(),
                error(code, message) { return Error(IRQRegistrationStatus, message, IRQ_REGISTRATION_ERROR); }
            );
        },
        none_do_nothing()
    );
    return Success(IRQRegistrationStatus, IRQ_REGISTERED);
}

Result(IRQRegistrationStatus) IRQDispatcher__dispose_process_resources(IRQDispatcherPtr self, ProcessPtr process)
{
    if (self == NULL) // hundelint: ignore-no-null
    {
        return Error(IRQRegistrationStatus, missing_dispatcher, IRQ_REGISTRATION_ERROR);
    }
    if (process == NULL) // hundelint: ignore-no-null
    {
        return Error(IRQRegistrationStatus, missing_service_process, IRQ_INVALID_CONSUMER);
    }
    if (is_nothing(self->interrupt_controller))
    {
        return Error(IRQRegistrationStatus, missing_interrupt_controller, IRQ_REGISTRATION_ERROR);
    }

    InterruptControllerInfoPtr interrupt_controller = &self->interrupt_controller.some.val;

    IRQNumbers stale_irqs = IRQNumbers__new();

    critical_section(self->lock, {
        for_each_map(IRQConsumerMap, self->consumers, index, irq, consumer, {
            if ((consumer.type == IRQ_CONSUMER_SERVICE || consumer.type == IRQ_CONSUMER_MANAGER) &&
                consumer.service_process == process)
            {
                match_result(
                    IRQNumbers__push(&stale_irqs, irq),
                    success_do_nothing(),
                    error(code, message) { cs_return Error(IRQRegistrationStatus, message, IRQ_REGISTRATION_ERROR); }
                );
            }
        });
        for_each_fa(IRQNumbers, stale_irqs, irq_index, irq, {
            IRQConsumerMap__delete(&self->consumers, irq);
            Arch__mask_irq(interrupt_controller, irq);
        });
    });

    Optional(UserAddress) manager_memory = None(UserAddress);
    critical_section(self->lock, {
        match_optional(
            self->manager,
            some(manager)
            {
                if (manager == process)
                {
                    manager_memory = self->manager_queue_memory;
                    self->manager = None(ProcessPtr);
                    self->manager_queue_memory = None(UserAddress);
                }
            },
            none_do_nothing()
        );
    });
    match_optional(
        manager_memory,
        some(memory)
        {
            PageTable__unmap(&process->page_table, process->kernel, memory.virtual_addr,
                             KERNEL_NOTIFICATION_QUEUE_PAGE_SIZE);
            match_result(
                Kernel__free(memory.kernel_addr.virtual_addr),
                success_do_nothing(),
                error(code, message) { return Error(IRQRegistrationStatus, message, IRQ_REGISTRATION_ERROR); }
            );
        },
        none_do_nothing()
    );

    match_result(
        ServiceEventDispatcher__unsubscribe_process(&self->scheduler->service_event_dispatcher, process),
        success_do_nothing(),
        error(code, message) { return Error(IRQRegistrationStatus, message, IRQ_REGISTRATION_ERROR); }
    );

    return Success(IRQRegistrationStatus, IRQ_REGISTERED);
}

Result(IRQDispatchStatus) IRQDispatcher__dispatch(IRQDispatcherPtr self, IRQNumber irq, IRQDispatchContextPtr context)
{
    if (irq >= 32)
    {
        __d_printf(IODEV, "irq dispatcher dispatch irq=%i\n", irq);
    }
    if (self == NULL) // hundelint: ignore-no-null
    {
        return Error(IRQDispatchStatus, missing_dispatcher, IRQ_DISPATCH_ERROR);
    }

    match_optional(
        IRQConsumerMap__get_ptr(&self->consumers, irq),
        some(consumer)
        {
            if (consumer->type == IRQ_CONSUMER_KERNEL)
            {
                return consumer->kernel_consumer(context, consumer->kernel_data);
            }

            if (consumer->type == IRQ_CONSUMER_SERVICE)
            {
                __d_printf(IODEV, "irq dispatcher service irq=%i\n", irq);
                match_result(
                    Scheduler__send_irq_event(self->scheduler, irq),
                    success(_) { return Success(IRQDispatchStatus, IRQ_DISPATCH_RESCHEDULE); },
                    error(code, message) { return Error(IRQDispatchStatus, message, IRQ_DISPATCH_ERROR); }
                );
            }

            if (consumer->type == IRQ_CONSUMER_MANAGER)
            {
                InterruptControllerInfoPtr interrupt_controller = &self->interrupt_controller.some.val;
                Arch__mask_irq(interrupt_controller, irq);
                consumer->delivery_sequence++;
                if (consumer->delivery_sequence == 0)
                {
                    consumer->delivery_sequence++;
                }
                match_optional(
                    self->manager_queue_memory,
                    some(memory)
                    {
                        KernelNotificationQueuePtr queue = k_v_ptr(memory, KernelNotificationQueue);
                        match_result(
                            KernelNotificationQueue__publish(queue, irq, consumer->delivery_sequence),
                            success_do_nothing(),
                            error(code, message) { return Error(IRQDispatchStatus, message, IRQ_DISPATCH_ERROR); }
                        );
                        IpcPlatform__signal_event();
                        return Success(IRQDispatchStatus, IRQ_DISPATCH_HANDLED);
                    },
                    none_do_nothing()
                );
                return Error(IRQDispatchStatus, interrupt_manager_required, IRQ_DISPATCH_ERROR);
            }
        },
        none_do_nothing()
    );

    return Success(IRQDispatchStatus, IRQ_DISPATCH_UNHANDLED);
}
