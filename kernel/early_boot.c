#include "early_boot.h"

KernelEarlyBootStatus KernelEarlyBoot__enter(ConstKernelEarlyBootInfoPtr self)
{
    if (self->system_info.cpu_count == 0)
    {
        return KERNEL_EARLY_BOOT_MISSING_CPUS;
    }

    if (self->system_info.physical_memory.physical_start >= self->system_info.physical_memory.physical_end)
    {
        return KERNEL_EARLY_BOOT_INVALID_MEMORY;
    }

    if (self->system_info.interrupt_controller.kind == INTERRUPT_CONTROLLER_NONE)
    {
        return KERNEL_EARLY_BOOT_MISSING_INTERRUPT_CONTROLLER;
    }

    return KERNEL_EARLY_BOOT_READY;
}
