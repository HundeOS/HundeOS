#ifndef __KERNEL_KERNEL_CORE_H
#define __KERNEL_KERNEL_CORE_H

#include <stdint.h>
#include "subsystems/io/io_subsystem.h"
#include "syscall/syscall_dispatcher.h"
#include "scheduler/scheduler.h"
#include "heap.h"
#include "mutex.h"
#include "subsystems/message/message_broker.h"
#include "irq/irq_dispatcher.h"
#include "system_info.h"
#include "syscall/auth.h"

typedef uint64_t KernelInitStatus;

typedef struct Kernel
{
    KernelInitStatus status;
    mutex_t init_lock;

    SyscallDispatcher syscall_dispatcher;
    IOSubsystemPtr io_subsystem;
    Optional(SystemInfo) system_info;
    Heap heap;
    SchedulerPtr scheduler;
    MessageBrokerPtr message_broker;
    IRQDispatcherPtr irq_dispatcher;
    Auth auth;
} Kernel;

typedef Kernel * KernelPtr;

#endif
