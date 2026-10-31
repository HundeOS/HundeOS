#include "native.h"
#include "hunde/boot_info.h"
#include "sys/boot_info.h"

static RustBootDevice RustBootDevice__new(const BootDeviceInfo * info)
{
    return (RustBootDevice){.physical_base = info->physical_base, .mmio_size = info->mmio_size, .irq = info->irq};
}

static RustBootUart RustBootUart__new(const BootUartInfo * info)
{
    return (RustBootUart){.physical_base = info->physical_base,
                          .mmio_size = info->mmio_size,
                          .irq = info->irq,
                          .kind = info->kind,
                          .register_shift = info->register_shift,
                          .register_width = info->register_width};
}

static RustBootRtc RustBootRtc__new(const BootRtcInfo * info)
{
    return (RustBootRtc){.device = RustBootDevice__new(&info->device), .kind = info->kind};
}

static RustBootFramebuffer RustBootFramebuffer__new(const BootFramebufferInfo * info)
{
    return (RustBootFramebuffer){.physical_base = info->physical_base,
                                 .size = info->size,
                                 .width = info->width,
                                 .height = info->height,
                                 .pixels_per_scanline = info->pixels_per_scanline,
                                 .pixel_format = info->pixel_format,
                                 .red_mask = info->red_mask,
                                 .green_mask = info->green_mask,
                                 .blue_mask = info->blue_mask,
                                 .reserved_mask = info->reserved_mask};
}

static RustBootPci RustBootPci__new(const BootPciInfo * info)
{
    return (RustBootPci){.ecam_physical_base = info->ecam_physical_base,
                         .ecam_size = info->ecam_size,
                         .bus_start = info->bus_start,
                         .bus_end = info->bus_end};
}

static RustBootSdHost RustBootSdHost__new(const BootSdHostInfo * info)
{
    return (RustBootSdHost){
        .device = RustBootDevice__new(&info->device), .kind = info->kind, .source_clock_hz = info->source_clock_hz};
}

static RustBootEthernet RustBootEthernet__new(const BootEthernetInfo * info)
{
    return (RustBootEthernet){.device = RustBootDevice__new(&info->device), .kind = info->kind};
}

static RustBootGic RustBootGic__new(const BootGicInfo * info)
{
    return (RustBootGic){.distributor_physical_base = info->distributor_physical_base,
                         .redistributor_physical_base = info->redistributor_physical_base,
                         .cpu_interface_physical_base = info->cpu_interface_physical_base};
}

static RustBootPlic RustBootPlic__new(const BootPlicInfo * info)
{
    return (RustBootPlic){.physical_base = info->physical_base, .source_count = info->source_count};
}

static RustBootVirtioMmio RustBootVirtioMmio__new(const BootVirtioMmioInfo * info)
{
    return (RustBootVirtioMmio){.physical_base = info->physical_base,
                                .slot_size = info->slot_size,
                                .slot_count = info->slot_count,
                                .first_irq = info->first_irq};
}

static RustBootInfoResult RustBootInfoResult__new_error(int32_t code)
{
    return (RustBootInfoResult){.status = RustNative__error(code)};
}

static RustBootInfoResult RustBootInfoResult__new(BootHardwareInfoPtr info)
{
    RustBootInfoResult result = {.status = HundeStatusResult__success(),
                                 .info = {.cpu_count = info->cpu_count,
                                          .online_cpu_count = info->online_cpu_count,
                                          .uart = RustBootUart__new(&info->uart),
                                          .memory_start = info->memory_start,
                                          .memory_end = info->memory_end,
                                          .controller_kind = info->interrupt_controller.kind}};
    match_optional(
        info->rtc,
        some(value)
        {
            result.info.rtc_present = 1;
            result.info.rtc = RustBootRtc__new(&value);
        },
        none_do_nothing()
    );
    match_optional(
        info->framebuffer,
        some(value)
        {
            result.info.framebuffer_present = 1;
            result.info.framebuffer = RustBootFramebuffer__new(&value);
        },
        none_do_nothing()
    );
    match_optional(
        info->pci,
        some(value)
        {
            result.info.pci_present = 1;
            result.info.pci = RustBootPci__new(&value);
        },
        none_do_nothing()
    );
    match_optional(
        info->sd_host,
        some(value)
        {
            result.info.sd_host_present = 1;
            result.info.sd_host = RustBootSdHost__new(&value);
        },
        none_do_nothing()
    );
    match_optional(
        info->ethernet,
        some(value)
        {
            result.info.ethernet_present = 1;
            result.info.ethernet = RustBootEthernet__new(&value);
        },
        none_do_nothing()
    );
    match_optional(
        info->virtio_mmio,
        some(value)
        {
            result.info.virtio_mmio_present = 1;
            result.info.virtio_mmio = RustBootVirtioMmio__new(&value);
        },
        none_do_nothing()
    );
    if (info->interrupt_controller.kind == INTERRUPT_CONTROLLER_GIC_V2 ||
        info->interrupt_controller.kind == INTERRUPT_CONTROLLER_GIC_V3)
    {
        result.info.gic_present = 1;
        result.info.gic = RustBootGic__new(&info->interrupt_controller.details.gic);
    }
    else if (info->interrupt_controller.kind == INTERRUPT_CONTROLLER_PLIC)
    {
        result.info.plic_present = 1;
        result.info.plic = RustBootPlic__new(&info->interrupt_controller.details.plic);
    }
    return result;
}

RustBootInfoResult hunde_rust_boot_info(void)
{
    match_result(
        BootHardwareInfo__load(),
        success(info)
        {
            return RustBootInfoResult__new(&info);
        },
        error(code, message)
        {
            return RustBootInfoResult__new_error(code);
        }
    );
    return RustBootInfoResult__new_error(RESULT_UNKNOWN_ERROR_CODE);
}
