#include "system_info.h"
#include "platform.h"

#define RISCV_PAGE_SIZE 4096ULL

ResultDef(PhysicalMemoryRegions);

static Address RiscVPhysicalMemory__align_down(Address value)
{
    return value & ~(RISCV_PAGE_SIZE - 1U);
}

static Address RiscVPhysicalMemory__align_up_to_limit(Address value, Address limit)
{
    if (value >= limit || value > UINT64_MAX - (RISCV_PAGE_SIZE - 1U))
    {
        return limit;
    }

    Address aligned = (value + RISCV_PAGE_SIZE - 1U) & ~(RISCV_PAGE_SIZE - 1U);
    return aligned < limit ? aligned : limit;
}

static Result(PhysicalMemoryRegions)
    RiscVPhysicalMemory__subtract(ConstPhysicalMemoryRegionsPtr self, ConstPhysicalMemoryRegionPtr exclusion)
{
    PhysicalMemoryRegions remaining = PhysicalMemoryRegions__new();
    for_each_fa_ptr(
        PhysicalMemoryRegions,
        self,
        index,
        region,
        if (exclusion->physical_end <= region.physical_start || exclusion->physical_start >= region.physical_end)
        {
            match_result(
                PhysicalMemoryRegions__push(&remaining, region),
                success_do_nothing(),
                error(code, message)
                {
                    return Error(PhysicalMemoryRegions, bstr("Physical memory regions full"), ARRAY_FULL);
                }
            );
            continue;
        }

    if (exclusion->physical_start > region.physical_start)
    {
        PhysicalMemoryRegion before = PhysicalMemoryRegion__new(region.physical_start, exclusion->physical_start);
        match_result(
            PhysicalMemoryRegions__push(&remaining, before),
            success_do_nothing(),
            error(code, message)
            {
                return Error(PhysicalMemoryRegions, bstr("Physical memory regions full"), ARRAY_FULL);
            }
        );
    }
    if (exclusion->physical_end < region.physical_end)
    {
        PhysicalMemoryRegion after = PhysicalMemoryRegion__new(exclusion->physical_end, region.physical_end);
        match_result(
            PhysicalMemoryRegions__push(&remaining, after),
            success_do_nothing(),
            error(code, message)
            {
                return Error(PhysicalMemoryRegions, bstr("Physical memory regions full"), ARRAY_FULL);
            }
        );
    }
    );
    return Success(PhysicalMemoryRegions, remaining);
}

static PhysicalMemoryRegions RiscVPlatformInfo__free_regions(ConstRiscVPlatformInfoPtr self,
                                                             ConstRiscVKernelLayoutPtr layout)
{
    PhysicalMemoryRegions regions = PhysicalMemoryRegions__new();
    Address memory_end = self->memory_base + self->memory_size;
    Address free_start = RiscVPhysicalMemory__align_up_to_limit(layout->image.physical_end, memory_end);
    Address free_end = RiscVPhysicalMemory__align_down(memory_end);
    if (free_start >= free_end)
    {
        return regions;
    }

    PhysicalMemoryRegion initial = PhysicalMemoryRegion__new(free_start, free_end);
    match_result(
        PhysicalMemoryRegions__push(&regions, initial),
        success_do_nothing(),
        error(code, message)
        {
            return PhysicalMemoryRegions__new();
        }
    );

    for_each_fa_ptr(
        PhysicalMemoryRegions,
        &self->reserved_memory_regions,
        index,
        reserved,
                    Address reserved_start = RiscVPhysicalMemory__align_down(reserved.physical_start);
                Address reserved_end = RiscVPhysicalMemory__align_up_to_limit(reserved.physical_end, free_end);
                PhysicalMemoryRegion exclusion = PhysicalMemoryRegion__new(reserved_start, reserved_end);
                regions =
                    unwrap_result(RiscVPhysicalMemory__subtract(&regions, &exclusion), PhysicalMemoryRegions__new());
    )
    ;

    Address stack_start = RiscVPhysicalMemory__align_down(layout->stack.physical_start);
    Address stack_end = RiscVPhysicalMemory__align_up_to_limit(layout->stack.physical_end, free_end);
    PhysicalMemoryRegion stack = PhysicalMemoryRegion__new(stack_start, stack_end);
    return unwrap_result(RiscVPhysicalMemory__subtract(&regions, &stack), PhysicalMemoryRegions__new());
}

SystemInfo RiscVPlatformInfo__system_info(ConstRiscVPlatformInfoPtr self, ConstRiscVKernelLayoutPtr layout,
                                          uint64_t boot_hart_id)
{
    SystemInfo info = SystemInfo__new();
    info.cpu_count = self->cpu_count;
    info.timer_frequency = self->timebase_frequency;
    info.wait_mode = BOOT_WAIT_SPIN;
    info.physical_memory.physical_start = self->memory_base;
    info.physical_memory.physical_end = self->memory_base + self->memory_size;
    info.physical_memory.free_regions = RiscVPlatformInfo__free_regions(self, layout);
    info.kernel_image = layout->image;
    info.embedded_binaries = layout->embedded_binaries;
    info.kernel_heap = layout->heap;
    info.kernel_stack = layout->stack;
    Address uart_virtual_base = RISCV_HIGHER_HALF_OFFSET + self->uart_base;
    info.mmio = MemoryRegion__new(uart_virtual_base, self->uart_base, self->uart_base + self->uart_size);
    info.trace_uart = UartInfo__new(self->uart_base, uart_virtual_base, self->uart_size, self->uart_irq,
                                    UART_KIND_NS16550, self->uart_register_shift, self->uart_register_width);
    info.console_uart = info.trace_uart;
    if (self->rtc_size != 0)
    {
        BootDeviceInfo device = BootDeviceInfo__new(self->rtc_base, self->rtc_size, self->rtc_irq);
        info.rtc = Some(BootRtcInfo, BootRtcInfo__new(&device, RTC_KIND_GOLDFISH));
    }
    if (self->virtio_mmio_count != 0)
    {
        BootVirtioMmioInfo virtio_mmio = BootVirtioMmioInfo__new(
            self->virtio_mmio_base, self->virtio_mmio_size, self->virtio_mmio_count, self->virtio_mmio_first_irq);
        info.virtio_mmio = Some(BootVirtioMmioInfo, virtio_mmio);
    }
    info.framebuffer = None(FramebufferInfo);
    if (self->pci_ecam_size != 0)
    {
        info.pci = Some(PciInfo,
                        PciInfo__new(self->pci_ecam_base, self->pci_ecam_size, self->pci_bus_start, self->pci_bus_end));
    }
    if (self->sd_host_size != 0)
    {
        info.sd_host = Some(SdHostInfo, SdHostInfo__new(self->sd_host_base, self->sd_host_size, self->sd_host_irq,
                                                       self->sd_host_kind, self->sd_host_source_clock_hz));
    }
    info.ethernet_mac = None(EthernetMacInfo);
    PlicInterruptControllerInfo plic = PlicInterruptControllerInfo__new(
        self->plic_base, RISCV_HIGHER_HALF_OFFSET + self->plic_base, self->plic_source_count, boot_hart_id);
    info.interrupt_controller = InterruptControllerInfo__new_plic(&plic);
    return info;
}
