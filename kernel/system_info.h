#ifndef __KERNEL_SYSTEM_INFO_H
#define __KERNEL_SYSTEM_INFO_H

#include "address.h"
#include "types/fixed_array.h"
#include "types/optional.h"
#include "hardware/boot_info.h"
#include "hardware/ethernet.h"

#define MAX_PHYSICAL_MEMORY_REGIONS 64

typedef struct MemoryRegion
{
    Address virtual_start;
    Address physical_start;
    Address physical_end;
} MemoryRegion;

typedef struct PhysicalMemoryRegion
{
    Address physical_start;
    Address physical_end;
} PhysicalMemoryRegion;

static inline PhysicalMemoryRegion PhysicalMemoryRegion__new(Address physical_start, Address physical_end)
{
    return (PhysicalMemoryRegion){
        .physical_start = physical_start,
        .physical_end = physical_end,
    };
}

OptionalDef(PhysicalMemoryRegion);

bool PhysicalMemoryRegion__equals(void * a, void * b);

FixedArrayDefine(PhysicalMemoryRegion, MAX_PHYSICAL_MEMORY_REGIONS, PhysicalMemoryRegions,
                 PhysicalMemoryRegion__equals);
typedef PhysicalMemoryRegions * PhysicalMemoryRegionsPtr;

typedef struct UartInfo
{
    Address physical_base;
    Address virtual_base;
    uint64_t irq;
} UartInfo;

typedef enum FramebufferPixelFormat
{
    FRAMEBUFFER_PIXEL_FORMAT_RGB_RESERVED_8BIT_PER_COLOR = 0,
    FRAMEBUFFER_PIXEL_FORMAT_BGR_RESERVED_8BIT_PER_COLOR = 1,
    FRAMEBUFFER_PIXEL_FORMAT_BIT_MASK = 2,
    FRAMEBUFFER_PIXEL_FORMAT_BLT_ONLY = 3,
    FRAMEBUFFER_PIXEL_FORMAT_UNKNOWN = 0xff,
} FramebufferPixelFormat;

typedef struct FramebufferInfo
{
    Address physical_base;
    uint64_t size;
    uint32_t width;
    uint32_t height;
    uint32_t pixels_per_scanline;
    FramebufferPixelFormat pixel_format;
    uint32_t red_mask;
    uint32_t green_mask;
    uint32_t blue_mask;
    uint32_t reserved_mask;
} FramebufferInfo;

static inline FramebufferInfo FramebufferInfo__new(Address physical_base, uint64_t size, uint32_t width,
                                                   uint32_t height, uint32_t pixels_per_scanline,
                                                   FramebufferPixelFormat pixel_format, uint32_t red_mask,
                                                   uint32_t green_mask, uint32_t blue_mask, uint32_t reserved_mask)
{
    return (FramebufferInfo){
        .physical_base = physical_base,
        .size = size,
        .width = width,
        .height = height,
        .pixels_per_scanline = pixels_per_scanline,
        .pixel_format = pixel_format,
        .red_mask = red_mask,
        .green_mask = green_mask,
        .blue_mask = blue_mask,
        .reserved_mask = reserved_mask,
    };
}

OptionalDef(FramebufferInfo);

typedef struct PciInfo
{
    Address ecam_physical_base;
    uint64_t ecam_size;
    uint64_t bus_start;
    uint64_t bus_end;
} PciInfo;

static inline PciInfo PciInfo__new(Address ecam_physical_base, uint64_t ecam_size, uint64_t bus_start, uint64_t bus_end)
{
    return (PciInfo){
        .ecam_physical_base = ecam_physical_base,
        .ecam_size = ecam_size,
        .bus_start = bus_start,
        .bus_end = bus_end,
    };
}

OptionalDef(PciInfo);

typedef struct SdHostInfo
{
    Address physical_base;
    uint64_t mmio_size;
    uint64_t irq;
} SdHostInfo;

static inline SdHostInfo SdHostInfo__new(Address physical_base, uint64_t mmio_size, uint64_t irq)
{
    return (SdHostInfo){
        .physical_base = physical_base,
        .mmio_size = mmio_size,
        .irq = irq,
    };
}

OptionalDef(SdHostInfo);

typedef struct EthernetMacInfo
{
    EthernetMacKind kind;
    Address physical_base;
    uint64_t mmio_size;
    uint64_t irq;
} EthernetMacInfo;

static inline EthernetMacInfo EthernetMacInfo__new(EthernetMacKind kind, Address physical_base, uint64_t mmio_size,
                                                   uint64_t irq)
{
    return (EthernetMacInfo){
        .kind = kind,
        .physical_base = physical_base,
        .mmio_size = mmio_size,
        .irq = irq,
    };
}

OptionalDef(EthernetMacInfo);

typedef struct GicInfo
{
    Address distributor_physical_base;
    Address distributor_virtual_base;
    Address redistributor_physical_base;
    Address redistributor_virtual_base;
} GicInfo;

typedef struct PhysicalMemoryInfo
{
    Address physical_start;
    Address physical_end;
    PhysicalMemoryRegions free_regions;
} PhysicalMemoryInfo;

typedef enum InterruptControllerKind
{
    INTERRUPT_CONTROLLER_NONE = 0,
    INTERRUPT_CONTROLLER_GIC_V2 = 1,
    INTERRUPT_CONTROLLER_GIC_V3 = 2,
} InterruptControllerKind;

typedef struct InterruptControllerInfo
{
    InterruptControllerKind kind;
    Address distributor_physical_base;
    Address distributor_virtual_base;
    Address redistributor_physical_base;
    Address redistributor_virtual_base;
    Address cpu_interface_physical_base;
    Address cpu_interface_virtual_base;
} InterruptControllerInfo;

OptionalDef(InterruptControllerInfo);

typedef struct SystemInfo
{
    uint64_t cpu_count;
    BootWaitMode wait_mode;
    size_t heap_coarse_block_size;
    PhysicalMemoryInfo physical_memory;
    MemoryRegion kernel_image;
    MemoryRegion kernel_heap;
    MemoryRegion kernel_stack;
    MemoryRegion embedded_binaries;
    MemoryRegion mmio;
    UartInfo uart;
    Optional(FramebufferInfo) framebuffer;
    Optional(PciInfo) pci;
    Optional(SdHostInfo) sd_host;
    Optional(EthernetMacInfo) ethernet_mac;
    GicInfo gic;
    InterruptControllerInfo interrupt_controller;
} SystemInfo;

OptionalDef(SystemInfo);

typedef MemoryRegion * MemoryRegionPtr;
typedef PhysicalMemoryRegion * PhysicalMemoryRegionPtr;
typedef UartInfo * UartInfoPtr;
typedef FramebufferInfo * FramebufferInfoPtr;
typedef PciInfo * PciInfoPtr;
typedef SdHostInfo * SdHostInfoPtr;
typedef EthernetMacInfo * EthernetMacInfoPtr;
typedef GicInfo * GicInfoPtr;
typedef PhysicalMemoryInfo * PhysicalMemoryInfoPtr;
typedef InterruptControllerInfo * InterruptControllerInfoPtr;
typedef SystemInfo * SystemInfoPtr;

static inline size_t SystemInfo__default_heap_coarse_block_size(void)
{
    return 4ULL * 1024ULL * 1024ULL;
}

static inline SystemInfo SystemInfo__new()
{
    SystemInfo info = {};
    info.heap_coarse_block_size = SystemInfo__default_heap_coarse_block_size();
    return info;
}

KernelAddress SystemInfo__kernel_address_for_physical(SystemInfoPtr self, Address physical);
KernelAddress SystemInfo__kernel_address_for_virtual(SystemInfoPtr self, Address virtual_addr);

#endif
