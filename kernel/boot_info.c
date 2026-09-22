#include "boot_info.h"

BootHardwareInfo BootHardwareInfo__from_system(SystemInfoPtr info, uint64_t online_cpu_count)
{
    BootHardwareInfo result = BootHardwareInfo__new();
    result.cpu_count = info->cpu_count;
    result.online_cpu_count = online_cpu_count;
    result.timer_frequency = info->timer_frequency;
    result.wait_mode = info->wait_mode;
    result.uart = BootUartInfo__new(info->uart.physical_base, 0x1000, info->uart.irq, info->uart.kind);
    result.virtio_mmio = info->virtio_mmio;
    result.memory_start = info->physical_memory.physical_start;
    result.memory_end = info->physical_memory.physical_end;
    if (info->interrupt_controller.kind == INTERRUPT_CONTROLLER_GIC_V2 ||
        info->interrupt_controller.kind == INTERRUPT_CONTROLLER_GIC_V3)
    {
        GicInterruptControllerInfo gic = info->interrupt_controller.details.gic;
        BootGicInfo boot_gic = BootGicInfo__new(gic.distributor_physical_base, gic.redistributor_physical_base,
                                                gic.cpu_interface_physical_base);
        result.interrupt_controller = BootInterruptControllerInfo__new_gic(info->interrupt_controller.kind, &boot_gic);
    }
    else if (info->interrupt_controller.kind == INTERRUPT_CONTROLLER_PLIC)
    {
        PlicInterruptControllerInfo plic = info->interrupt_controller.details.plic;
        BootPlicInfo boot_plic = BootPlicInfo__new(plic.physical_base, plic.source_count);
        result.interrupt_controller = BootInterruptControllerInfo__new_plic(&boot_plic);
    }
    match_optional(
        info->framebuffer,
        some(value)
        {
            BootFramebufferInfo framebuffer = BootFramebufferInfo__new(
                value.physical_base, value.size, value.width, value.height, value.pixels_per_scanline,
                value.pixel_format, value.red_mask, value.green_mask, value.blue_mask, value.reserved_mask);
            result.framebuffer = Some(BootFramebufferInfo, framebuffer);
        },
        none_do_nothing()
    );
    match_optional(
        info->pci,
        some(value)
        {
            BootPciInfo pci =
                BootPciInfo__new(value.ecam_physical_base, value.ecam_size, value.bus_start, value.bus_end);
            result.pci = Some(BootPciInfo, pci);
        },
        none_do_nothing()
    );
    match_optional(
        info->sd_host,
        some(value)
        {
            BootDeviceInfo device = BootDeviceInfo__new(value.physical_base, value.mmio_size, value.irq);
            result.sd_host = Some(BootDeviceInfo, device);
        },
        none_do_nothing()
    );
    match_optional(
        info->ethernet_mac,
        some(value)
        {
            BootDeviceInfo device = BootDeviceInfo__new(value.physical_base, value.mmio_size, value.irq);
            BootEthernetInfo ethernet = BootEthernetInfo__new(&device, value.kind);
            result.ethernet = Some(BootEthernetInfo, ethernet);
        },
        none_do_nothing()
    );
    return result;
}
