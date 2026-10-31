#ifndef HUNDE_RUST_BOOT_INFO_H
#define HUNDE_RUST_BOOT_INFO_H

#include "host_component.h"

typedef struct RustBootDevice
{
    uint64_t physical_base;
    uint64_t mmio_size;
    uint64_t irq;
} RustBootDevice;

typedef struct RustBootUart
{
    uint64_t physical_base;
    uint64_t mmio_size;
    uint64_t irq;
    uint64_t kind;
    uint64_t register_shift;
    uint64_t register_width;
} RustBootUart;

typedef struct RustBootRtc
{
    RustBootDevice device;
    uint64_t kind;
} RustBootRtc;

typedef struct RustBootFramebuffer
{
    uint64_t physical_base;
    uint64_t size;
    uint64_t width;
    uint64_t height;
    uint64_t pixels_per_scanline;
    uint64_t pixel_format;
    uint64_t red_mask;
    uint64_t green_mask;
    uint64_t blue_mask;
    uint64_t reserved_mask;
} RustBootFramebuffer;

typedef struct RustBootPci
{
    uint64_t ecam_physical_base;
    uint64_t ecam_size;
    uint64_t bus_start;
    uint64_t bus_end;
} RustBootPci;

typedef struct RustBootSdHost
{
    RustBootDevice device;
    uint64_t kind;
    uint64_t source_clock_hz;
} RustBootSdHost;

typedef struct RustBootEthernet
{
    RustBootDevice device;
    uint64_t kind;
} RustBootEthernet;

typedef struct RustBootGic
{
    uint64_t distributor_physical_base;
    uint64_t redistributor_physical_base;
    uint64_t cpu_interface_physical_base;
} RustBootGic;

typedef struct RustBootPlic
{
    uint64_t physical_base;
    uint64_t source_count;
} RustBootPlic;

typedef struct RustBootVirtioMmio
{
    uint64_t physical_base;
    uint64_t slot_size;
    uint64_t slot_count;
    uint64_t first_irq;
} RustBootVirtioMmio;

typedef struct RustBootHardwareInfo
{
    uint64_t cpu_count;
    uint64_t online_cpu_count;
    RustBootUart uart;
    uint64_t rtc_present;
    RustBootRtc rtc;
    uint64_t framebuffer_present;
    RustBootFramebuffer framebuffer;
    uint64_t pci_present;
    RustBootPci pci;
    uint64_t sd_host_present;
    RustBootSdHost sd_host;
    uint64_t ethernet_present;
    RustBootEthernet ethernet;
    uint64_t memory_start;
    uint64_t memory_end;
    uint64_t controller_kind;
    uint64_t gic_present;
    RustBootGic gic;
    uint64_t plic_present;
    RustBootPlic plic;
    uint64_t virtio_mmio_present;
    RustBootVirtioMmio virtio_mmio;
} RustBootHardwareInfo;

typedef struct RustBootInfoResult
{
    HundeStatusResult status;
    RustBootHardwareInfo info;
} RustBootInfoResult;

RustBootInfoResult hunde_rust_boot_info(void);
_Static_assert(sizeof(RustBootHardwareInfo) == 440, "Boot information ABI changed");
_Static_assert(sizeof(RustBootInfoResult) == 472, "Boot result ABI changed");
_Static_assert(offsetof(RustBootInfoResult, info) == 32, "Boot result offset changed");
#endif
