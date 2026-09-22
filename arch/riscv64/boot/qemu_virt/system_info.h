#ifndef __ARCH_RISCV64_BOOT_QEMU_VIRT_SYSTEM_INFO_H
#define __ARCH_RISCV64_BOOT_QEMU_VIRT_SYSTEM_INFO_H

#include "device_tree.h"
#include "../../../../kernel/system_info.h"

typedef struct RiscVKernelLayout
{
    MemoryRegion image;
    MemoryRegion embedded_binaries;
    MemoryRegion heap;
    MemoryRegion stack;
} RiscVKernelLayout;

typedef const RiscVKernelLayout * ConstRiscVKernelLayoutPtr;

static inline RiscVKernelLayout RiscVKernelLayout__new(MemoryRegion image, MemoryRegion embedded_binaries,
                                                       MemoryRegion heap, MemoryRegion stack)
{
    return (RiscVKernelLayout){
        .image = image,
        .embedded_binaries = embedded_binaries,
        .heap = heap,
        .stack = stack,
    };
}

SystemInfo RiscVPlatformInfo__system_info(ConstRiscVPlatformInfoPtr self, ConstRiscVKernelLayoutPtr layout,
                                          uint64_t boot_hart_id);

#endif
