#include <stdint.h>

#include "../boot.h"
#include "../boot_debug.h"
#include "../boot_strings.h"
#include "../cpu.h"
#include "../exceptions.h"
#include "../mmu.h"
#include "../platform.h"
#include "../print.h"
#include "../uart.h"
#include "../uefi.h"
#include "handoff.h"

#include "../../cpu.h"
#include "../../gicr.h"
#include "../../../../kernel/cpu.h"

extern const uint64_t __k_stack_start[];
extern const uint64_t __k_stack_phys_start[];
extern const uint64_t __k_stack_phys_end[];
extern const uint64_t vector_table;
extern const uint64_t __text_virt;
extern const uint64_t __text_phys;
extern const uint64_t __lma_end;

extern const uint64_t __embedded_binaries_start[];
extern const uint64_t __embedded_binaries_phys_start[];
extern const uint64_t __embedded_binaries_phys_end[];

extern char __heap_start[];
extern char __heap_start_phys[];
extern const uint64_t __mmio_end[];
extern const uint64_t __gicd_start[];

#define PI4_GICD_PHYS_BASE 0xFF841000ULL
#define PI4_GICR_PHYS_BASE 0xFF848000ULL
#define PI4_GICC_PHYS_BASE 0xFF842000ULL
#define PI4_UART_IRQ_ID 153ULL
#define PI4_EMMC2_PHYS_BASE 0xFE340000ULL
#define PI4_EMMC2_MMIO_SIZE 0x100ULL
#define PI4_EMMC2_IRQ_ID 126ULL
#define PI4_GENET_PHYS_BASE 0xFD580000ULL
#define PI4_GENET_MMIO_SIZE 0x10000ULL
#define PI4_GENET_IRQ_ID 157ULL
#define PI_UEFI_GICD_BASE ((uint64_t)&__gicd_start[0])
#define PI_UEFI_GICR_BASE (PI_UEFI_GICD_BASE + GICR_OFFSET)
__BOOT_DATA__(static BootPlatformInfo) pi_uefi_platform_info = { // hundelint: ignore-constructor-required
    .cpu_count = 4,
    .memory_physical_start = 0,
    .memory_physical_end = 4ULL * 1024ULL * 1024ULL * 1024ULL,
    .uart_physical_base = BOOT_UART0_PHYS_BASE,
    .uart_virtual_base = BOOT_MMIO_VIRT_BASE,
    .uart_irq = PI4_UART_IRQ_ID,
    .framebuffer = None(FramebufferInfo),
    .sd_host = Some(SdHostInfo, ((SdHostInfo){ // hundelint: ignore-constructor-required
                                 .physical_base = PI4_EMMC2_PHYS_BASE,
                                 .mmio_size = PI4_EMMC2_MMIO_SIZE,
                                 .irq = PI4_EMMC2_IRQ_ID,
                                 .kind = SD_HOST_KIND_GENERIC,
                                 .source_clock_hz = 0,
                             })),
    .interrupt_controller_kind = INTERRUPT_CONTROLLER_GIC_V2,
    .gic_distributor_physical_base = PI4_GICD_PHYS_BASE,
    .gic_distributor_virtual_base = PI_UEFI_GICD_BASE,
    .gic_redistributor_physical_base = PI4_GICR_PHYS_BASE,
    .gic_redistributor_virtual_base = PI_UEFI_GICR_BASE,
    .gic_cpu_interface_physical_base = PI4_GICC_PHYS_BASE,
    .gic_cpu_interface_virtual_base = PI_UEFI_GICD_BASE + (PI4_GICC_PHYS_BASE & ((2ULL * 1024ULL * 1024ULL) - 1ULL)),
};

__BOOT_DATA__(static SystemInfo) pi_uefi_system_info;
__BOOT_DATA__(static uint64_t) pi_uefi_image_handle = 0;
__BOOT_DATA__(static uint64_t) pi_uefi_system_table = 0;
__BOOT_DATA__(static Optional(RealtimeClockSample)) pi_uefi_realtime_clock;

static __BOOT__(void) PiUefiPlatformInfo__set_fixed_devices(BootPlatformInfoPtr self)
{
    self->ethernet_mac =
        Some(EthernetMacInfo, EthernetMacInfo__new(ETHERNET_MAC_KIND_BCM2711_GENET, PI4_GENET_PHYS_BASE,
                                                   PI4_GENET_MMIO_SIZE, PI4_GENET_IRQ_ID));
}

__BOOT__(void)

pi_uefi_set_entry_args(uint64_t image_handle, uint64_t system_table, uint64_t physical_memory_end,
                       uint64_t handoff_magic, uint64_t handoff_address)
{
    pi_uefi_image_handle = image_handle;
    pi_uefi_system_table = system_table;
    pi_uefi_realtime_clock = None(RealtimeClockSample);
    if (handoff_magic == PI_UEFI_HANDOFF_MAGIC && physical_memory_end != 0 && handoff_address != 0)
    {
        pi_uefi_platform_info.memory_physical_end = physical_memory_end;
        PiUefiHandoffPtr handoff = (PiUefiHandoffPtr)handoff_address;
        uint64_t region_count = handoff->physical_memory_region_count;
        if (region_count > PhysicalMemoryRegions__max_size)
        {
            region_count = PhysicalMemoryRegions__max_size;
        }

        pi_uefi_platform_info.physical_memory_regions.len = region_count;
        for (uint64_t index = 0; index < region_count; index++)
        {
            pi_uefi_platform_info.physical_memory_regions.data[index] =
                PhysicalMemoryRegion__new(handoff->physical_memory_regions[index].physical_start,
                                          handoff->physical_memory_regions[index].physical_end);
        }

        if (handoff->framebuffer_present != 0)
        {
            pi_uefi_platform_info.framebuffer =
                Some(FramebufferInfo,
                     FramebufferInfo__new(handoff->framebuffer.physical_base, handoff->framebuffer.size,
                                          handoff->framebuffer.width, handoff->framebuffer.height,
                                          handoff->framebuffer.pixels_per_scanline,
                                          (FramebufferPixelFormat)handoff->framebuffer.pixel_format,
                                          handoff->framebuffer.red_mask, handoff->framebuffer.green_mask,
                                          handoff->framebuffer.blue_mask, handoff->framebuffer.reserved_mask));
        }
        if (handoff->realtime_clock_present != 0)
        {
            pi_uefi_realtime_clock =
                Some(RealtimeClockSample,
                     RealtimeClockSample__new(handoff->realtime_unix_usec, handoff->realtime_monotonic_usec));
        }
    }
}

__BOOT__(SystemInfoPtr) pi_uefi_system_info_init(void)
{
    PiUefiPlatformInfo__set_fixed_devices(&pi_uefi_platform_info);
    UefiAcpi__apply_system_table(&pi_uefi_platform_info, pi_uefi_system_table);

    pi_uefi_system_info.cpu_count = pi_uefi_platform_info.cpu_count;
    pi_uefi_system_info.wait_mode = BOOT_WAIT_WFE;
    pi_uefi_system_info.heap_coarse_block_size = SystemInfo__default_heap_coarse_block_size();

    pi_uefi_system_info.physical_memory.physical_start = pi_uefi_platform_info.memory_physical_start;
    pi_uefi_system_info.physical_memory.physical_end = pi_uefi_platform_info.memory_physical_end;
    pi_uefi_system_info.physical_memory.free_regions.len = 0;

    pi_uefi_system_info.kernel_image.virtual_start = (uint64_t)&__text_virt;
    pi_uefi_system_info.kernel_image.physical_start = (uint64_t)&__text_phys;
    pi_uefi_system_info.kernel_image.physical_end = (uint64_t)&__lma_end;

    pi_uefi_system_info.kernel_heap.virtual_start = (uint64_t)&__heap_start[0];
    pi_uefi_system_info.kernel_heap.physical_start = (uint64_t)&__heap_start_phys[0];
    pi_uefi_system_info.kernel_heap.physical_end = pi_uefi_platform_info.memory_physical_end;

    pi_uefi_system_info.kernel_stack.virtual_start = (uint64_t)&__k_stack_start[0];
    pi_uefi_system_info.kernel_stack.physical_start = (uint64_t)&__k_stack_phys_start[0];
    pi_uefi_system_info.kernel_stack.physical_end = (uint64_t)&__k_stack_phys_end[0];

    pi_uefi_system_info.embedded_binaries.virtual_start = (uint64_t)&__embedded_binaries_start[0];
    pi_uefi_system_info.embedded_binaries.physical_start = (uint64_t)&__embedded_binaries_phys_start[0];
    pi_uefi_system_info.embedded_binaries.physical_end = (uint64_t)&__embedded_binaries_phys_end[0];

    pi_uefi_system_info.mmio.virtual_start = (uint64_t)&__mmio_start[0];
    pi_uefi_system_info.mmio.physical_start = pi_uefi_platform_info.uart_physical_base;
    pi_uefi_system_info.mmio.physical_end =
        pi_uefi_platform_info.uart_physical_base + ((uint64_t)&__mmio_end[0] - (uint64_t)&__mmio_start[0]);

    pi_uefi_system_info.trace_uart =
        UartInfo__new(pi_uefi_platform_info.uart_physical_base, pi_uefi_platform_info.uart_virtual_base, 0x1000,
                      pi_uefi_platform_info.uart_irq, UART_KIND_PL011, 0, 4);
    pi_uefi_system_info.console_uart = pi_uefi_system_info.trace_uart;
    pi_uefi_system_info.rtc = None(BootRtcInfo);
    pi_uefi_system_info.realtime_clock = pi_uefi_realtime_clock;
    pi_uefi_system_info.virtio_mmio = None(BootVirtioMmioInfo);
    pi_uefi_system_info.framebuffer = pi_uefi_platform_info.framebuffer;
    pi_uefi_system_info.pci = pi_uefi_platform_info.pci;
    pi_uefi_system_info.sd_host = pi_uefi_platform_info.sd_host;
    pi_uefi_system_info.ethernet_mac = pi_uefi_platform_info.ethernet_mac;

    GicInterruptControllerInfo gic = GicInterruptControllerInfo__new(
        pi_uefi_platform_info.gic_distributor_physical_base,
        PI_UEFI_GICD_BASE + (pi_uefi_platform_info.gic_distributor_physical_base & ((2ULL * 1024ULL * 1024ULL) - 1ULL)),
        pi_uefi_platform_info.gic_redistributor_physical_base, pi_uefi_platform_info.gic_redistributor_virtual_base,
        pi_uefi_platform_info.gic_cpu_interface_physical_base, pi_uefi_platform_info.gic_cpu_interface_virtual_base);
    pi_uefi_system_info.interrupt_controller =
        InterruptControllerInfo__new_gic(pi_uefi_platform_info.interrupt_controller_kind, &gic);

    pi_uefi_system_info.physical_memory.free_regions.len = pi_uefi_platform_info.physical_memory_regions.len;
    for (uint64_t index = 0; index < pi_uefi_platform_info.physical_memory_regions.len; index++)
    {
        pi_uefi_system_info.physical_memory.free_regions.data[index] =
            pi_uefi_platform_info.physical_memory_regions.data[index];
    }

    return &pi_uefi_system_info;
}

__BOOT__(void) pi_uefi_enter_kernel(uint64_t cpu)
{
    cpu_set_this_id(cpu);
    boot_debug_print(new_line);
    boot_debug_print(entering_hi_mem);
    boot_debug_print_base(cpu, 10);
    boot_debug_print(new_line);

    uint64_t stack_top = (uint64_t)&__k_stack_start[0] + ((cpu + 1) * STACK_SIZE);
    enter_kernel(stack_top, (uint64_t)&vector_table, (uint64_t)&CPU__enter_kernel, &pi_uefi_system_info);
}

__BOOT__(void) primary_booted(void)
{
    CPUID cpu = rd_mpidr() & 0xff;
    pi_uefi_system_info_init();
    BootUart__set_physical_base(pi_uefi_platform_info.uart_physical_base);
    BootUart__init();

    boot_debug_print(current_el);
    boot_debug_print_base((read_current_el() >> 2), 10);
    boot_debug_print(new_line);
    boot_debug_print(booting);
    boot_debug_print(new_line);

    boot_vectors_init();
    setup_boot_mappings(&pi_uefi_platform_info, &pi_uefi_system_info);
    enable_fp_simd();
    enable_tbi_for_kernel();
    enable_mmu();
    CPU__set_boot_id(cpu);

    boot_secondary_cpus(pi_uefi_platform_info.cpu_count);

    pi_uefi_enter_kernel(cpu);
}

__BOOT__(void) secondary_booted(uint64_t cpu)
{
    boot_debug_print(new_line);
    boot_debug_print(secondary_booting);
    boot_debug_print_base(cpu, 10);
    boot_debug_print(new_line);

    pi_uefi_enter_kernel(cpu);
}
