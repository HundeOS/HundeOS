#ifndef __KERNEL_KERNEL_CORE_H
#define __KERNEL_KERNEL_CORE_H

#include <stdint.h>
#include "subsystems/io/io_subsystem.h"
#include "syscall/syscall_dispatcher.h"
#include "heap.h"
#include "mutex.h"

typedef uint64_t KernelInitStatus;

typedef struct Kernel
{
    KernelInitStatus status;
    mutex_t init_lock;

    SyscallDispatcher syscall_dispatcher;
    IOSubsystem io_subsystem;
    Heap heap;
} Kernel;

typedef Kernel * KernelPtr;

#endif