#ifndef HUND_HARDWARE_BOOT_INFO_H
#define HUND_HARDWARE_BOOT_INFO_H

#include "types/address.h"
#include "types/result.h"

typedef enum BootWaitMode
{
    BOOT_WAIT_SPIN = 0,
    BOOT_WAIT_WFE = 1,
} BootWaitMode;

typedef struct BootDeviceInfo
{
    Address physical_base;
    uint64_t mmio_size;
    uint64_t irq;
} BootDeviceInfo;
typedef const BootDeviceInfo * ConstBootDeviceInfoPtr;
static inline BootDeviceInfo BootDeviceInfo__new(Address physical_base, uint64_t mmio_size, uint64_t irq)
{
    return (BootDeviceInfo){physical_base, mmio_size, irq};
}

typedef struct BootFramebufferInfo
{
    Address physical_base;
    uint64_t size;
    uint64_t width;
    uint64_t height;
    uint64_t pixels_per_scanline;
    uint64_t pixel_format;
    uint64_t red_mask;
    uint64_t green_mask;
    uint64_t blue_mask;
    uint64_t reserved_mask;
} BootFramebufferInfo;
OptionalDef(BootFramebufferInfo);
static inline BootFramebufferInfo BootFramebufferInfo__new(Address physical_base, uint64_t size, uint64_t width,
                                                           uint64_t height, uint64_t stride, uint64_t format,
                                                           uint64_t red, uint64_t green, uint64_t blue,
                                                           uint64_t reserved)
{
    return (BootFramebufferInfo){physical_base, size, width, height, stride, format, red, green, blue, reserved};
}
OptionalDef(BootDeviceInfo);

typedef struct BootPciInfo
{
    Address ecam_physical_base;
    uint64_t ecam_size;
    uint64_t bus_start;
    uint64_t bus_end;
} BootPciInfo;
OptionalDef(BootPciInfo);
static inline BootPciInfo BootPciInfo__new(Address base, uint64_t size, uint64_t first, uint64_t last)
{
    return (BootPciInfo){base, size, first, last};
}

typedef struct BootEthernetInfo
{
    BootDeviceInfo device;
    uint64_t kind;
} BootEthernetInfo;
OptionalDef(BootEthernetInfo);
static inline BootEthernetInfo BootEthernetInfo__new(ConstBootDeviceInfoPtr device, uint64_t kind)
{
    return (BootEthernetInfo){*device, kind};
}

typedef struct BootHardwareInfo
{
    uint64_t cpu_count;
    uint64_t online_cpu_count;
    BootWaitMode wait_mode;
    BootDeviceInfo uart;
    Optional(BootFramebufferInfo) framebuffer;
    Optional(BootPciInfo) pci;
    Optional(BootDeviceInfo) sd_host;
    Optional(BootEthernetInfo) ethernet;
    Address memory_start;
    Address memory_end;
    uint64_t interrupt_controller_kind;
    Address interrupt_distributor;
    Address interrupt_redistributor;
    Address interrupt_cpu_interface;
} BootHardwareInfo;
typedef BootHardwareInfo * BootHardwareInfoPtr;
typedef const BootHardwareInfo * ConstBootHardwareInfoPtr;
ResultDef(BootHardwareInfo);
static inline bool BootHardwareInfo__has_framebuffer(ConstBootHardwareInfoPtr self)
{
    match_optional(self->framebuffer,
        some(value)
        {
            return true;
        },
        none()
        {
            return false;
        }
    );
}
static inline BootHardwareInfo BootHardwareInfo__new(void)
{
    return (BootHardwareInfo){.wait_mode = BOOT_WAIT_SPIN,
                              .framebuffer = None(BootFramebufferInfo),
                              .pci = None(BootPciInfo),
                              .sd_host = None(BootDeviceInfo),
                              .ethernet = None(BootEthernetInfo)};
}

#endif
