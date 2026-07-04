#include "irq_dispatcher.h"

static KString missing_dispatcher = KSTRING_NEW("Missing IRQ dispatcher");
static KString missing_kernel_consumer = KSTRING_NEW("Missing IRQ kernel consumer");
static KString missing_service_process = KSTRING_NEW("Missing IRQ service process");
static KString missing_scheduler = KSTRING_NEW("Missing scheduler for IRQ service consumer");
static KString irq_already_registered = KSTRING_NEW("IRQ already registered");

MapBodyDefine(IRQConsumerMap);

Result(IRQRegistrationStatus) IRQDispatcher__register_kernel(
    IRQDispatcherPtr self, IRQNumber irq, IRQKernelConsumer consumer, void * data
)
{
    if (self == NULL)
    {
        return Error(IRQRegistrationStatus, missing_dispatcher, IRQ_REGISTRATION_ERROR);
    }
    if (consumer == NULL)
    {
        return Error(IRQRegistrationStatus, missing_kernel_consumer, IRQ_INVALID_CONSUMER);
    }

    IRQConsumer irq_consumer = (IRQConsumer) {
        .type = IRQ_CONSUMER_KERNEL,
        .kernel_consumer = consumer,
        .kernel_data = data,
        .service_process = NULL,
    };

    CriticalSection(self->lock,
    {
        MapPtrMatch(IRQConsumerMap, IRQConsumerMap__get_ptr(&self->consumers, irq))
            IsSome(existing, _cs_return Error(IRQRegistrationStatus, irq_already_registered, IRQ_ALREADY_REGISTERED))
        End
        IRQConsumerMap__set(&self->consumers, irq, irq_consumer);
    });

    return Success(IRQRegistrationStatus, IRQ_REGISTERED);
}

Result(IRQRegistrationStatus) IRQDispatcher__register_service(
    IRQDispatcherPtr self, IRQNumber irq, ProcessPtr process
)
{
    if (self == NULL)
    {
        return Error(IRQRegistrationStatus, missing_dispatcher, IRQ_REGISTRATION_ERROR);
    }
    if (process == NULL)
    {
        return Error(IRQRegistrationStatus, missing_service_process, IRQ_INVALID_CONSUMER);
    }
    if (self->scheduler == NULL)
    {
        return Error(IRQRegistrationStatus, missing_scheduler, IRQ_REGISTRATION_ERROR);
    }

    IRQConsumer irq_consumer = (IRQConsumer) {
        .type = IRQ_CONSUMER_SERVICE,
        .kernel_consumer = NULL,
        .kernel_data = NULL,
        .service_process = process,
    };

    CriticalSection(self->lock,
    {
        MapPtrMatch(IRQConsumerMap, IRQConsumerMap__get_ptr(&self->consumers, irq))
            IsSome(existing, _cs_return Error(IRQRegistrationStatus, irq_already_registered, IRQ_ALREADY_REGISTERED))
        End
        IRQConsumerMap__set(&self->consumers, irq, irq_consumer);
    });
    __d_printf(IODEV, "irq register service irq=%i pid=%i name=%s\n", irq, process->pid, cstr(process->name));

    ResultMatch(ServiceCode, Scheduler__subscribe_irq(self->scheduler, process, irq))
        IsError(code, message, return Error(IRQRegistrationStatus, message, IRQ_REGISTRATION_ERROR))
    End

    ResultMatch(SubsystemInitStatus, Arch__enable_irq(irq))
        IsError(code, message, return Error(IRQRegistrationStatus, message, IRQ_REGISTRATION_ERROR))
    End

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
    if (self == NULL)
    {
        return Error(IRQDispatchStatus, missing_dispatcher, IRQ_DISPATCH_ERROR);
    }

    MapPtrMatch(IRQConsumerMap, IRQConsumerMap__get_ptr(&self->consumers, irq))
        IsSome(consumer,
        {
            if (consumer->type == IRQ_CONSUMER_KERNEL)
            {
                return consumer->kernel_consumer(context, consumer->kernel_data);
            }

            if (consumer->type == IRQ_CONSUMER_SERVICE)
            {
                __d_printf(IODEV, "irq dispatcher service irq=%i\n", irq);
                ResultMatch(ServiceCode, Scheduler__send_irq_event(self->scheduler, irq))
                    IsSuccess(_, return Success(IRQDispatchStatus, IRQ_DISPATCH_RESCHEDULE))
                    IsError(code, message, return Error(IRQDispatchStatus, message, IRQ_DISPATCH_ERROR))
                End
            }
        })
    End

    return Success(IRQDispatchStatus, IRQ_DISPATCH_UNHANDLED);
}
