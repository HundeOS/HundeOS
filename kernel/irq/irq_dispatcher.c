#include "irq_dispatcher.h"

static BString missing_dispatcher = BSTRING_NEW("Missing IRQ dispatcher");
static BString missing_kernel_consumer = BSTRING_NEW("Missing IRQ kernel consumer");
static BString missing_service_process = BSTRING_NEW("Missing IRQ service process");
static BString missing_scheduler = BSTRING_NEW("Missing scheduler for IRQ service consumer");
static BString missing_interrupt_controller = BSTRING_NEW("Missing interrupt controller info");
static BString irq_already_registered = BSTRING_NEW("IRQ already registered");

MapBodyDefine(IRQConsumerMap);
FixedArrayDefine(IRQNumber, 256, IRQNumbers, SimpleEquals(uint64_t));
FixedArrayBodyDefine(IRQNumbers);

Result(IRQRegistrationStatus) IRQDispatcher__register_kernel(
    IRQDispatcherPtr self, IRQNumber irq, IRQKernelConsumer consumer, void * data
)
{
    if (self == NULL) // hundelint: ignore-no-null
    {
        return Error(IRQRegistrationStatus, missing_dispatcher, IRQ_REGISTRATION_ERROR);
    }
    if (consumer == NULL) // hundelint: ignore-no-null
    {
        return Error(IRQRegistrationStatus, missing_kernel_consumer, IRQ_INVALID_CONSUMER);
    }

    IRQConsumer irq_consumer = (IRQConsumer) {
        .type = IRQ_CONSUMER_KERNEL,
        .kernel_consumer = consumer,
        .kernel_data = data,
        .service_process = NULL, // hundelint: ignore-no-null
    };

    critical_section(self->lock,
    {
        match_optional(IRQConsumerMap__value_type_ptr, IRQConsumerMap__get_ptr(&self->consumers, irq),
            some(existing) { cs_return Error(IRQRegistrationStatus, irq_already_registered, IRQ_ALREADY_REGISTERED); },
            none_do_nothing()
        );
        IRQConsumerMap__set(&self->consumers, irq, irq_consumer);
    });

    return Success(IRQRegistrationStatus, IRQ_REGISTERED);
}

Result(IRQRegistrationStatus) IRQDispatcher__register_service(
    IRQDispatcherPtr self, IRQNumber irq, ProcessPtr process
)
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

    IRQConsumer irq_consumer = (IRQConsumer) {
        .type = IRQ_CONSUMER_SERVICE,
        .kernel_consumer = NULL, // hundelint: ignore-no-null
        .kernel_data = NULL, // hundelint: ignore-no-null
        .service_process = process,
    };

    critical_section(self->lock,
    {
        match_optional(IRQConsumerMap__value_type_ptr, IRQConsumerMap__get_ptr(&self->consumers, irq),
            some(existing) { cs_return Error(IRQRegistrationStatus, irq_already_registered, IRQ_ALREADY_REGISTERED); },
            none_do_nothing()
        );
        IRQConsumerMap__set(&self->consumers, irq, irq_consumer);
    });
    __d_printf(IODEV, "irq register service irq=%i pid=%i name=%s\n", irq, process->pid, cstr(process->name));

    match_result(ServiceCode, Scheduler__subscribe_irq(self->scheduler, process, irq),
        success_do_nothing(),
        error(code, message) { return Error(IRQRegistrationStatus, message, IRQ_REGISTRATION_ERROR); }
    );

    match_result(SubsystemInitStatus, Arch__enable_irq(interrupt_controller, irq),
        success_do_nothing(),
        error(code, message) { return Error(IRQRegistrationStatus, message, IRQ_REGISTRATION_ERROR); }
    );

    return Success(IRQRegistrationStatus, IRQ_REGISTERED);
}

Result(IRQRegistrationStatus) IRQDispatcher__dispose_process_resources(
    IRQDispatcherPtr self, ProcessPtr process
)
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

    critical_section(self->lock,
    {
        for_each_map(IRQConsumerMap, self->consumers, index, irq, consumer,
        {
            if (consumer.type == IRQ_CONSUMER_SERVICE && consumer.service_process == process)
            {
                match_result(CollectionCode, IRQNumbers__push(&stale_irqs, irq),
                    success_do_nothing(),
                    error(code, message) { cs_return Error(IRQRegistrationStatus, message, IRQ_REGISTRATION_ERROR); }
                );
            }

        });
        for_each_fa(IRQNumbers, stale_irqs, irq_index, irq,
        {
            IRQConsumerMap__delete(&self->consumers, irq);
            Arch__mask_irq(interrupt_controller, irq);
        });
    });

    match_result(ServiceCode, ServiceEventDispatcher__unsubscribe_process(&self->scheduler->service_event_dispatcher, process),
        success_do_nothing(),
        error(code, message) { return Error(IRQRegistrationStatus, message, IRQ_REGISTRATION_ERROR); }
    );

    return Success(IRQRegistrationStatus, IRQ_REGISTERED);
}

Result(IRQDispatchStatus) IRQDispatcher__dispatch(
    IRQDispatcherPtr self, IRQNumber irq, IRQDispatchContextPtr context
)
{
    if (irq >= 32)
    {
        __d_printf(IODEV, "irq dispatcher dispatch irq=%i\n", irq);
    }
    if (self == NULL) // hundelint: ignore-no-null
    {
        return Error(IRQDispatchStatus, missing_dispatcher, IRQ_DISPATCH_ERROR);
    }

    match_optional(IRQConsumerMap__value_type_ptr, IRQConsumerMap__get_ptr(&self->consumers, irq),
        some(consumer)
        {
            if (consumer->type == IRQ_CONSUMER_KERNEL)
            {
                return consumer->kernel_consumer(context, consumer->kernel_data);
            }

            if (consumer->type == IRQ_CONSUMER_SERVICE)
            {
                __d_printf(IODEV, "irq dispatcher service irq=%i\n", irq);
                match_result(ServiceCode, Scheduler__send_irq_event(self->scheduler, irq),
                    success(_) { return Success(IRQDispatchStatus, IRQ_DISPATCH_RESCHEDULE); },
                    error(code, message) { return Error(IRQDispatchStatus, message, IRQ_DISPATCH_ERROR); }
                );
            }
        },
        none_do_nothing()
    );

    return Success(IRQDispatchStatus, IRQ_DISPATCH_UNHANDLED);
}
