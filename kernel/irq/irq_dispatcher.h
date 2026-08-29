#ifndef __KERNEL_IRQ_IRQ_DISPATCHER_H
#define __KERNEL_IRQ_IRQ_DISPATCHER_H

#include <stdbool.h>
#include <stdint.h>

#include "../arch/init.h"
#include "../mutex.h"
#include "../scheduler/scheduler.h"
#include "../system_info.h"
#include "../types/map.h"
#include "../types/result.h"

typedef enum IRQDispatchStatus
{
    IRQ_DISPATCH_HANDLED = 0,
    IRQ_DISPATCH_UNHANDLED = 1,
    IRQ_DISPATCH_RESCHEDULE = 2,
    IRQ_DISPATCH_ERROR = -1,
}
IRQDispatchStatus;

ResultDef(IRQDispatchStatus);

typedef enum IRQRegistrationStatus
{
    IRQ_REGISTERED = 0,
    IRQ_ALREADY_REGISTERED = -1,
    IRQ_INVALID_CONSUMER = -2,
    IRQ_REGISTRATION_ERROR = -3,
}
IRQRegistrationStatus;

ResultDef(IRQRegistrationStatus);

typedef struct IRQDispatchContext
{
    IRQNumber irq;
    CPUID cpu;
    bool current_el;
    ProcessContextPtr process_context;
}
IRQDispatchContext;

typedef IRQDispatchContext * IRQDispatchContextPtr;

static inline IRQDispatchContext IRQDispatchContext__new(
    IRQNumber irq,
    CPUID cpu,
    bool current_el,
    ProcessContextPtr process_context)
{
    return (IRQDispatchContext) {
        .irq = irq,
        .cpu = cpu,
        .current_el = current_el,
        .process_context = process_context,
    };
}

typedef Result(IRQDispatchStatus) (*IRQKernelConsumer)(IRQDispatchContextPtr context, void * data);

typedef enum IRQConsumerType
{
    IRQ_CONSUMER_KERNEL = 1,
    IRQ_CONSUMER_SERVICE = 2,
}
IRQConsumerType;

typedef struct IRQConsumer
{
    IRQConsumerType type;
    IRQKernelConsumer kernel_consumer;
    void * kernel_data;
    ProcessPtr service_process;
}
IRQConsumer;

typedef IRQConsumer * IRQConsumerPtr;

OptionalDef(IRQConsumer);
MapDefine(IRQNumber, IRQConsumer, 256, IRQConsumerMap, SimpleEquals(uint64_t));

typedef struct IRQDispatcher
{
    SchedulerPtr scheduler;
    Optional(InterruptControllerInfo) interrupt_controller;
    IRQConsumerMap consumers;
    mutex_t lock;
}
IRQDispatcher;

typedef IRQDispatcher * IRQDispatcherPtr;

#define NEW_IRQ_DISPATCHER(scheduler_ptr) \
    (IRQDispatcher) { \
        .scheduler = scheduler_ptr, \
        .interrupt_controller = None(InterruptControllerInfo), \
        .consumers = EmptyMap(IRQConsumerMap), \
        .lock = MUTEX_INIT, \
    }

Result(IRQRegistrationStatus) IRQDispatcher__register_kernel(
    IRQDispatcherPtr self, IRQNumber irq, IRQKernelConsumer consumer, void * data
);

Result(IRQRegistrationStatus) IRQDispatcher__register_service(
    IRQDispatcherPtr self, IRQNumber irq, ProcessPtr process
);
Result(IRQRegistrationStatus) IRQDispatcher__dispose_process_resources(
    IRQDispatcherPtr self, ProcessPtr process
);

Result(IRQDispatchStatus) IRQDispatcher__dispatch(
    IRQDispatcherPtr self, IRQNumber irq, IRQDispatchContextPtr context
);

#endif
