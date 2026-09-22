#ifndef __KERNEL_EARLY_BOOT_H
#define __KERNEL_EARLY_BOOT_H

#include <stdint.h>

#include "system_info.h"

typedef struct KernelEarlyBootInfo
{
    uint64_t hardware_thread_id;
    SystemInfo system_info;
} KernelEarlyBootInfo;

typedef const KernelEarlyBootInfo * ConstKernelEarlyBootInfoPtr;

typedef enum KernelEarlyBootStatus
{
    KERNEL_EARLY_BOOT_READY = 0,
    KERNEL_EARLY_BOOT_MISSING_CPUS,
    KERNEL_EARLY_BOOT_INVALID_MEMORY,
    KERNEL_EARLY_BOOT_MISSING_INTERRUPT_CONTROLLER,
} KernelEarlyBootStatus;

inline KernelEarlyBootInfo KernelEarlyBootInfo__new(uint64_t hardware_thread_id, SystemInfo system_info)
{
    return (KernelEarlyBootInfo){
        .hardware_thread_id = hardware_thread_id,
        .system_info = system_info,
    };
}

KernelEarlyBootStatus KernelEarlyBoot__enter(ConstKernelEarlyBootInfoPtr self);

#endif
