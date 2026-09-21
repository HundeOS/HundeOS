#ifndef __KERNEL_KERNEL_CORE_H
#define __KERNEL_KERNEL_CORE_H

#include <stdint.h>
#include "syscall/syscall_dispatcher.h"
#include "scheduler/scheduler.h"
#include "heap.h"
#include "mutex.h"
#include "irq/irq_dispatcher.h"
#include "system_info.h"
#include "syscall/auth.h"
#include "service/service_directory.h"
#include "service/device_registry.h"
#include "memory_accounting.h"
#include "service/process_registry.h"

typedef uint64_t KernelInitStatus;

typedef struct Kernel
{
    KernelInitStatus status;
    mutex_t init_lock;

    SyscallDispatcher syscall_dispatcher;
    Optional(SystemInfo) system_info;
    Heap heap;
    SchedulerPtr scheduler;
    IRQDispatcherPtr irq_dispatcher;
    KernelServiceDirectory service_directory;
    KernelDeviceRegistry device_registry;
    KernelProcessRegistry process_registry;
    MemoryAccounting memory_accounting;
    Auth auth;
    ProcessID boot_authority;
    bool boot_complete;
} Kernel;

typedef Kernel * KernelPtr;

#endif
