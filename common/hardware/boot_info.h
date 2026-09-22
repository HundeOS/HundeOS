#ifndef HUND_HARDWARE_BOOT_INFO_H
#define HUND_HARDWARE_BOOT_INFO_H

#include "types/address.h"
#include "types/result.h"

typedef enum BootWaitMode
{
    BOOT_WAIT_SPIN = 0,
    BOOT_WAIT_WFE = 1,
} BootWaitMode;

typedef enum UartKind
{
    UART_KIND_UNKNOWN = 0,
    UART_KIND_PL011 = 1,
    UART_KIND_NS16550 = 2,
} UartKind;

typedef enum InterruptControllerKind
{
    INTERRUPT_CONTROLLER_NONE = 0,
    INTERRUPT_CONTROLLER_GIC_V2 = 1,
    INTERRUPT_CONTROLLER_GIC_V3 = 2,
    INTERRUPT_CONTROLLER_PLIC = 3,
} InterruptControllerKind;

typedef struct BootGicInfo
{
    Address distributor_physical_base;
    Address redistributor_physical_base;
    Address cpu_interface_physical_base;
} BootGicInfo;
typedef const BootGicInfo * ConstBootGicInfoPtr;

static inline BootGicInfo BootGicInfo__new(Address distributor_physical_base, Address redistributor_physical_base,
                                           Address cpu_interface_physical_base)
{
    return (BootGicInfo){
        .distributor_physical_base = distributor_physical_base,
        .redistributor_physical_base = redistributor_physical_base,
        .cpu_interface_physical_base = cpu_interface_physical_base,
    };
}

typedef struct BootPlicInfo
{
    Address physical_base;
    uint64_t source_count;
} BootPlicInfo;
typedef const BootPlicInfo * ConstBootPlicInfoPtr;

static inline BootPlicInfo BootPlicInfo__new(Address physical_base, uint64_t source_count)
{
    return (BootPlicInfo){
        .physical_base = physical_base,
        .source_count = source_count,
    };
}

typedef union BootInterruptControllerDetails
{
    BootGicInfo gic;
    BootPlicInfo plic;
} BootInterruptControllerDetails;

typedef struct BootInterruptControllerInfo
{
    InterruptControllerKind kind;
    BootInterruptControllerDetails details;
} BootInterruptControllerInfo;

static inline BootInterruptControllerInfo BootInterruptControllerInfo__new_none(void)
{
    return (BootInterruptControllerInfo){};
}

static inline BootInterruptControllerInfo BootInterruptControllerInfo__new_gic(InterruptControllerKind kind,
                                                                               ConstBootGicInfoPtr gic)
{
    return (BootInterruptControllerInfo){
        .kind = kind,
        .details.gic = *gic,
    };
}

static inline BootInterruptControllerInfo BootInterruptControllerInfo__new_plic(ConstBootPlicInfoPtr plic)
{
    return (BootInterruptControllerInfo){
        .kind = INTERRUPT_CONTROLLER_PLIC,
        .details.plic = *plic,
    };
}

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

typedef struct BootUartInfo
{
    Address physical_base;
    uint64_t mmio_size;
    uint64_t irq;
    UartKind kind;
} BootUartInfo;
typedef const BootUartInfo * ConstBootUartInfoPtr;
static inline BootUartInfo BootUartInfo__new(Address physical_base, uint64_t mmio_size, uint64_t irq, UartKind kind)
{
    return (BootUartInfo){
        .physical_base = physical_base,
        .mmio_size = mmio_size,
        .irq = irq,
        .kind = kind,
    };
}

typedef struct BootVirtioMmioInfo
{
    Address physical_base;
    uint64_t slot_size;
    uint64_t slot_count;
    uint64_t first_irq;
} BootVirtioMmioInfo;
typedef const BootVirtioMmioInfo * ConstBootVirtioMmioInfoPtr;
OptionalDef(BootVirtioMmioInfo);
static inline BootVirtioMmioInfo BootVirtioMmioInfo__new(Address physical_base, uint64_t slot_size, uint64_t slot_count,
                                                         uint64_t first_irq)
{
    return (BootVirtioMmioInfo){physical_base, slot_size, slot_count, first_irq};
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
    uint64_t timer_frequency;
    BootWaitMode wait_mode;
    BootUartInfo uart;
    Optional(BootVirtioMmioInfo) virtio_mmio;
    Optional(BootFramebufferInfo) framebuffer;
    Optional(BootPciInfo) pci;
    Optional(BootDeviceInfo) sd_host;
    Optional(BootEthernetInfo) ethernet;
    Address memory_start;
    Address memory_end;
    BootInterruptControllerInfo interrupt_controller;
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
                              .interrupt_controller = BootInterruptControllerInfo__new_none(),
                              .virtio_mmio = None(BootVirtioMmioInfo),
                              .framebuffer = None(BootFramebufferInfo),
                              .pci = None(BootPciInfo),
                              .sd_host = None(BootDeviceInfo),
                              .ethernet = None(BootEthernetInfo)};
}

#endif
