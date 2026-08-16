#ifndef __KERNEL_SYSTEM_INFO_H
#define __KERNEL_SYSTEM_INFO_H

#include "address.h"
#include "types/fixed_array.h"
#include "types/optional.h"

#define MAX_PHYSICAL_MEMORY_REGIONS 64

typedef struct MemoryRegion {
    Address virtual_start;
    Address physical_start;
    Address physical_end;
} MemoryRegion;

typedef struct PhysicalMemoryRegion {
    Address physical_start;
    Address physical_end;
} PhysicalMemoryRegion;

OptionalDef(PhysicalMemoryRegion);

bool PhysicalMemoryRegion__equals(void * a, void * b);

FixedArrayDefine(
    PhysicalMemoryRegion,
    MAX_PHYSICAL_MEMORY_REGIONS,
    PhysicalMemoryRegions,
    PhysicalMemoryRegion__equals);

typedef struct UartInfo {
    Address physical_base;
    Address virtual_base;
    uint64_t irq;
} UartInfo;

typedef struct GicInfo {
    Address distributor_physical_base;
    Address distributor_virtual_base;
    Address redistributor_physical_base;
    Address redistributor_virtual_base;
} GicInfo;

typedef struct PhysicalMemoryInfo {
    Address physical_start;
    Address physical_end;
    PhysicalMemoryRegions free_regions;
} PhysicalMemoryInfo;

typedef enum InterruptControllerKind {
    INTERRUPT_CONTROLLER_NONE = 0,
    INTERRUPT_CONTROLLER_GIC_V2 = 1,
    INTERRUPT_CONTROLLER_GIC_V3 = 2,
} InterruptControllerKind;

typedef struct InterruptControllerInfo {
    InterruptControllerKind kind;
    Address distributor_physical_base;
    Address distributor_virtual_base;
    Address redistributor_physical_base;
    Address redistributor_virtual_base;
    Address cpu_interface_physical_base;
    Address cpu_interface_virtual_base;
} InterruptControllerInfo;

OptionalDef(InterruptControllerInfo);

typedef struct SystemInfo {
    uint64_t cpu_count;
    PhysicalMemoryInfo physical_memory;
    MemoryRegion kernel_image;
    MemoryRegion kernel_heap;
    MemoryRegion kernel_stack;
    MemoryRegion embedded_binaries;
    MemoryRegion mmio;
    UartInfo uart;
    GicInfo gic;
    InterruptControllerInfo interrupt_controller;
} SystemInfo;

OptionalDef(SystemInfo);

typedef MemoryRegion * MemoryRegionPtr;
typedef PhysicalMemoryRegion * PhysicalMemoryRegionPtr;
typedef UartInfo * UartInfoPtr;
typedef GicInfo * GicInfoPtr;
typedef PhysicalMemoryInfo * PhysicalMemoryInfoPtr;
typedef InterruptControllerInfo * InterruptControllerInfoPtr;
typedef SystemInfo * SystemInfoPtr;

KernelAddress SystemInfo__kernel_address_for_physical(SystemInfoPtr self, Address physical);
KernelAddress SystemInfo__kernel_address_for_virtual(SystemInfoPtr self, Address virtual_addr);

#endif
