#ifndef __AARCH64_BOOT_PLATFORM_H_
#define __AARCH64_BOOT_PLATFORM_H_

#include <stdint.h>
#include "../../../kernel/system_info.h"

#define BOOT_FRAMEBUFFER_MAP_SIZE (32ULL * 1024ULL * 1024ULL)

typedef struct BootPlatformInfo
{
    uint64_t cpu_count;
    uint64_t memory_physical_start;
    uint64_t memory_physical_end;
    PhysicalMemoryRegions physical_memory_regions;
    uint64_t uart_physical_base;
    uint64_t uart_virtual_base;
    uint64_t uart_irq;
    UartKind uart_kind;
    uint64_t uart_register_shift;
    uint64_t uart_register_width;
    Optional(FramebufferInfo) framebuffer;
    Optional(BootVirtioMmioInfo) virtio_mmio;
    Optional(PciInfo) pci;
    Optional(SdHostInfo) sd_host;
    Optional(EthernetMacInfo) ethernet_mac;
    InterruptControllerKind interrupt_controller_kind;
    uint64_t gic_distributor_physical_base;
    uint64_t gic_distributor_virtual_base;
    uint64_t gic_redistributor_physical_base;
    uint64_t gic_redistributor_virtual_base;
    uint64_t gic_cpu_interface_physical_base;
    uint64_t gic_cpu_interface_virtual_base;
} BootPlatformInfo;

typedef BootPlatformInfo * BootPlatformInfoPtr;

#endif
