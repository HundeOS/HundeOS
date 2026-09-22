#ifndef __ARCH_RISCV64_BOOT_QEMU_VIRT_DEVICE_TREE_H
#define __ARCH_RISCV64_BOOT_QEMU_VIRT_DEVICE_TREE_H

#include <stdint.h>

#include "../../../../kernel/system_info.h"

typedef struct RiscVPlatformInfo
{
    uint64_t cpu_count;
    uint64_t timebase_frequency;
    uint64_t memory_base;
    uint64_t memory_size;
    uint64_t uart_base;
    uint64_t uart_size;
    uint64_t uart_irq;
    uint64_t plic_base;
    uint64_t plic_size;
    uint64_t plic_source_count;
    uint64_t virtio_mmio_base;
    uint64_t virtio_mmio_size;
    uint64_t virtio_mmio_count;
    uint64_t virtio_mmio_first_irq;
    uint64_t pci_ecam_base;
    uint64_t pci_ecam_size;
    uint64_t pci_bus_start;
    uint64_t pci_bus_end;
    PhysicalMemoryRegions reserved_memory_regions;
} RiscVPlatformInfo;

typedef RiscVPlatformInfo * RiscVPlatformInfoPtr;
typedef const RiscVPlatformInfo * ConstRiscVPlatformInfoPtr;

static inline RiscVPlatformInfo RiscVPlatformInfo__new(void)
{
    return (RiscVPlatformInfo){
        .reserved_memory_regions = PhysicalMemoryRegions__new(),
    };
}

typedef enum RiscVDeviceTreeStatus
{
    RISCV_DEVICE_TREE_OK = 0,
    RISCV_DEVICE_TREE_INVALID_HEADER,
    RISCV_DEVICE_TREE_TRUNCATED,
    RISCV_DEVICE_TREE_MALFORMED,
    RISCV_DEVICE_TREE_TOO_DEEP,
    RISCV_DEVICE_TREE_UNSUPPORTED_CELLS,
    RISCV_DEVICE_TREE_MISSING_MEMORY,
    RISCV_DEVICE_TREE_MISSING_CPUS,
    RISCV_DEVICE_TREE_MISSING_TIMEBASE,
    RISCV_DEVICE_TREE_MISSING_UART,
    RISCV_DEVICE_TREE_MISSING_PLIC,
    RISCV_DEVICE_TREE_MISSING_VIRTIO,
    RISCV_DEVICE_TREE_MISSING_PCI,
    RISCV_DEVICE_TREE_TOO_MANY_RESERVED_REGIONS,
} RiscVDeviceTreeStatus;

typedef struct RiscVDeviceTreeResult
{
    RiscVDeviceTreeStatus status;
    RiscVPlatformInfo platform;
} RiscVDeviceTreeResult;

RiscVDeviceTreeResult RiscVDeviceTree__parse(uintptr_t device_tree);

#endif
