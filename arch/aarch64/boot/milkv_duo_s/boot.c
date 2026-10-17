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
extern const uint64_t __mmio_start[];
extern const uint64_t __mmio_end[];
extern const uint64_t __gicd_start[];
extern char __heap_start[];
extern char __heap_start_phys[];

#define DUO_S_MEMORY_START 0x80000000ULL
#define DUO_S_MEMORY_END 0x9fe00000ULL
#define DUO_S_UART0_PHYSICAL_BASE 0x04140000ULL
#define DUO_S_UART1_PHYSICAL_BASE 0x04150000ULL
#define DUO_S_UART0_IRQ 60ULL
#define DUO_S_UART1_IRQ 61ULL
#define DUO_S_PINMUX_BASE 0x03001000ULL
#define DUO_S_PINMUX_UART1_TX_OFFSET 0x64ULL
#define DUO_S_PINMUX_UART1_RX_OFFSET 0x68ULL
#define DUO_S_PINMUX_UART1_FUNCTION 6U
#define DUO_S_UART_CLOCK_HZ 25000000ULL
#define DUO_S_UART_BAUD 115200ULL
#define DUO_S_UART_RECEIVE_BUFFER 0ULL
#define DUO_S_UART_INTERRUPT_ENABLE 1ULL
#define DUO_S_UART_FIFO_CONTROL 2ULL
#define DUO_S_UART_LINE_CONTROL 3ULL
#define DUO_S_UART_MODEM_CONTROL 4ULL
#define DUO_S_UART_DIVISOR_FRACTION_OFFSET 0xc0ULL
#define DUO_S_UART_LINE_CONTROL_8N1 0x03U
#define DUO_S_UART_LINE_CONTROL_DLAB 0x80U
#define DUO_S_UART_FIFO_ENABLE_AND_CLEAR 0x07U
#define DUO_S_UART_MODEM_DTR_RTS 0x03U
#define DUO_S_MMIO_PHYSICAL_BASE 0x04000000ULL
#define DUO_S_MMIO_BLOCK_MASK ((2ULL * 1024ULL * 1024ULL) - 1ULL)
#define DUO_S_UART0_VIRTUAL_BASE ((uint64_t)&__mmio_start[0] + (DUO_S_UART0_PHYSICAL_BASE & DUO_S_MMIO_BLOCK_MASK))
#define DUO_S_UART1_VIRTUAL_BASE ((uint64_t)&__mmio_start[0] + (DUO_S_UART1_PHYSICAL_BASE & DUO_S_MMIO_BLOCK_MASK))
#define DUO_S_GICD_PHYSICAL_BASE 0x01f01000ULL
#define DUO_S_GICC_PHYSICAL_BASE 0x01f02000ULL
#define DUO_S_GIC_VIRTUAL_BLOCK ((uint64_t)&__gicd_start[0])
#define DUO_S_GICD_VIRTUAL_BASE (DUO_S_GIC_VIRTUAL_BLOCK + (DUO_S_GICD_PHYSICAL_BASE & DUO_S_MMIO_BLOCK_MASK))
#define DUO_S_GICC_VIRTUAL_BASE (DUO_S_GIC_VIRTUAL_BLOCK + (DUO_S_GICC_PHYSICAL_BASE & DUO_S_MMIO_BLOCK_MASK))
#define DUO_S_SDHCI_PHYSICAL_BASE 0x04310000ULL
#define DUO_S_SDHCI_IRQ 52ULL

typedef volatile uint32_t * MilkVDuoSRegisterPtr;

__BOOT_DATA__(static BootPlatformInfo) milkv_duo_s_platform_info;

__BOOT_DATA__(static SystemInfo) milkv_duo_s_system_info;

__BOOT__(void)

BootPlatform__set_entry_args(uint64_t dtb, uint64_t argument1, uint64_t argument2, uint64_t argument3,
                             uint64_t argument4)
{
}

static __BOOT__(void) MilkVDuoS__write32(uint64_t address, uint32_t value)
{
    *(MilkVDuoSRegisterPtr)address = value;
}

static __BOOT__(uint32_t) MilkVDuoS__read32(uint64_t address)
{
    return *(MilkVDuoSRegisterPtr)address;
}

static __BOOT__(uint64_t) MilkVDuoS__uart1_register(uint64_t index)
{
    return DUO_S_UART1_PHYSICAL_BASE + (index << 2U);
}

static __BOOT__(void) MilkVDuoS__configure_uart1(void)
{
    uint64_t baud_clock = DUO_S_UART_BAUD * 16ULL;
    uint32_t integer_divisor = (uint32_t)(DUO_S_UART_CLOCK_HZ / baud_clock);
    uint64_t remainder = DUO_S_UART_CLOCK_HZ % baud_clock;

    MilkVDuoS__write32(DUO_S_PINMUX_BASE + DUO_S_PINMUX_UART1_TX_OFFSET, DUO_S_PINMUX_UART1_FUNCTION);
    MilkVDuoS__write32(DUO_S_PINMUX_BASE + DUO_S_PINMUX_UART1_RX_OFFSET, DUO_S_PINMUX_UART1_FUNCTION);
    MilkVDuoS__write32(MilkVDuoS__uart1_register(DUO_S_UART_INTERRUPT_ENABLE), 0);
    MilkVDuoS__write32(DUO_S_UART1_PHYSICAL_BASE + DUO_S_UART_DIVISOR_FRACTION_OFFSET, UINT32_MAX);
    uint32_t fraction_mask = MilkVDuoS__read32(DUO_S_UART1_PHYSICAL_BASE + DUO_S_UART_DIVISOR_FRACTION_OFFSET);
    MilkVDuoS__write32(DUO_S_UART1_PHYSICAL_BASE + DUO_S_UART_DIVISOR_FRACTION_OFFSET, 0);

    uint32_t fraction = 0;
    if (fraction_mask != 0)
    {
        uint64_t fraction_scale = (uint64_t)fraction_mask + 1ULL;
        fraction = (uint32_t)((remainder * fraction_scale + baud_clock / 2ULL) / baud_clock);
        if ((uint64_t)fraction == fraction_scale)
        {
            integer_divisor++;
            fraction = 0;
        }
    }

    MilkVDuoS__write32(MilkVDuoS__uart1_register(DUO_S_UART_LINE_CONTROL), DUO_S_UART_LINE_CONTROL_DLAB);
    MilkVDuoS__write32(MilkVDuoS__uart1_register(DUO_S_UART_RECEIVE_BUFFER), integer_divisor & 0xffU);
    MilkVDuoS__write32(MilkVDuoS__uart1_register(DUO_S_UART_INTERRUPT_ENABLE), (integer_divisor >> 8U) & 0xffU);
    MilkVDuoS__write32(DUO_S_UART1_PHYSICAL_BASE + DUO_S_UART_DIVISOR_FRACTION_OFFSET, fraction);
    MilkVDuoS__write32(MilkVDuoS__uart1_register(DUO_S_UART_LINE_CONTROL), DUO_S_UART_LINE_CONTROL_8N1);
    MilkVDuoS__write32(MilkVDuoS__uart1_register(DUO_S_UART_FIFO_CONTROL), DUO_S_UART_FIFO_ENABLE_AND_CLEAR);
    MilkVDuoS__write32(MilkVDuoS__uart1_register(DUO_S_UART_MODEM_CONTROL), DUO_S_UART_MODEM_DTR_RTS);
    __asm__ volatile("dsb sy" ::: "memory");
}

static __BOOT__(void) MilkVDuoS__init_system_info(void)
{
    milkv_duo_s_platform_info.cpu_count = 1;
    milkv_duo_s_platform_info.memory_physical_start = DUO_S_MEMORY_START;
    milkv_duo_s_platform_info.memory_physical_end = DUO_S_MEMORY_END;
    milkv_duo_s_platform_info.uart_physical_base = DUO_S_UART1_PHYSICAL_BASE;
    milkv_duo_s_platform_info.uart_virtual_base = DUO_S_UART1_VIRTUAL_BASE;
    milkv_duo_s_platform_info.uart_irq = DUO_S_UART1_IRQ;
    milkv_duo_s_platform_info.uart_kind = UART_KIND_NS16550;
    milkv_duo_s_platform_info.uart_register_shift = 2;
    milkv_duo_s_platform_info.uart_register_width = 4;
    milkv_duo_s_platform_info.framebuffer = None(FramebufferInfo);
    milkv_duo_s_platform_info.pci = None(PciInfo);
    milkv_duo_s_platform_info.sd_host =
        Some(SdHostInfo,
             SdHostInfo__new(DUO_S_SDHCI_PHYSICAL_BASE, 0x1000, DUO_S_SDHCI_IRQ, SD_HOST_KIND_CV181X, 375000000));
    milkv_duo_s_platform_info.ethernet_mac = None(EthernetMacInfo);
    milkv_duo_s_platform_info.interrupt_controller_kind = INTERRUPT_CONTROLLER_GIC_V2;
    milkv_duo_s_platform_info.gic_distributor_physical_base = DUO_S_GICD_PHYSICAL_BASE;
    milkv_duo_s_platform_info.gic_distributor_virtual_base = DUO_S_GICD_VIRTUAL_BASE;
    milkv_duo_s_platform_info.gic_redistributor_physical_base = 0;
    milkv_duo_s_platform_info.gic_redistributor_virtual_base = 0;
    milkv_duo_s_platform_info.gic_cpu_interface_physical_base = DUO_S_GICC_PHYSICAL_BASE;
    milkv_duo_s_platform_info.gic_cpu_interface_virtual_base = DUO_S_GICC_VIRTUAL_BASE;

    milkv_duo_s_system_info.cpu_count = milkv_duo_s_platform_info.cpu_count;
    milkv_duo_s_system_info.wait_mode = BOOT_WAIT_WFE;
    milkv_duo_s_system_info.heap_coarse_block_size = SystemInfo__default_heap_coarse_block_size();
    milkv_duo_s_system_info.default_user_stack_size = 2ULL * 1024ULL * 1024ULL;

    milkv_duo_s_system_info.physical_memory.physical_start = milkv_duo_s_platform_info.memory_physical_start;
    milkv_duo_s_system_info.physical_memory.physical_end = milkv_duo_s_platform_info.memory_physical_end;
    milkv_duo_s_system_info.physical_memory.free_regions.len = 1;
    milkv_duo_s_system_info.physical_memory.free_regions.data[0] =
        PhysicalMemoryRegion__new((uint64_t)&__heap_start_phys[0], milkv_duo_s_platform_info.memory_physical_end);

    milkv_duo_s_system_info.kernel_image =
        MemoryRegion__new((uint64_t)&__text_virt, (uint64_t)&__text_phys, (uint64_t)&__lma_end);
    milkv_duo_s_system_info.kernel_heap =
        MemoryRegion__new((uint64_t)&__heap_start[0], (uint64_t)&__heap_start_phys[0], DUO_S_MEMORY_END);
    milkv_duo_s_system_info.kernel_stack = MemoryRegion__new(
        (uint64_t)&__k_stack_start[0], (uint64_t)&__k_stack_phys_start[0], (uint64_t)&__k_stack_phys_end[0]);
    milkv_duo_s_system_info.embedded_binaries =
        MemoryRegion__new((uint64_t)&__embedded_binaries_start[0], (uint64_t)&__embedded_binaries_phys_start[0],
                          (uint64_t)&__embedded_binaries_phys_end[0]);
    milkv_duo_s_system_info.mmio =
        MemoryRegion__new((uint64_t)&__mmio_start[0], DUO_S_MMIO_PHYSICAL_BASE,
                          DUO_S_MMIO_PHYSICAL_BASE + ((uint64_t)&__mmio_end[0] - (uint64_t)&__mmio_start[0]));

    milkv_duo_s_system_info.trace_uart = UartInfo__new(DUO_S_UART1_PHYSICAL_BASE, DUO_S_UART1_VIRTUAL_BASE, 0x1000,
                                                       DUO_S_UART1_IRQ, UART_KIND_NS16550, 2, 4);
    milkv_duo_s_system_info.console_uart = UartInfo__new(DUO_S_UART0_PHYSICAL_BASE, DUO_S_UART0_VIRTUAL_BASE, 0x1000,
                                                         DUO_S_UART0_IRQ, UART_KIND_NS16550, 2, 4);
    milkv_duo_s_system_info.virtio_mmio = None(BootVirtioMmioInfo);
    milkv_duo_s_system_info.framebuffer = None(FramebufferInfo);
    milkv_duo_s_system_info.pci = None(PciInfo);
    milkv_duo_s_system_info.sd_host = milkv_duo_s_platform_info.sd_host;
    milkv_duo_s_system_info.ethernet_mac = None(EthernetMacInfo);
    milkv_duo_s_system_info.rtc = None(BootRtcInfo);
    milkv_duo_s_system_info.realtime_clock = None(RealtimeClockSample);

    GicInterruptControllerInfo gic = GicInterruptControllerInfo__new(
        DUO_S_GICD_PHYSICAL_BASE, DUO_S_GICD_VIRTUAL_BASE, 0, 0, DUO_S_GICC_PHYSICAL_BASE, DUO_S_GICC_VIRTUAL_BASE);
    milkv_duo_s_system_info.interrupt_controller = InterruptControllerInfo__new_gic(INTERRUPT_CONTROLLER_GIC_V2, &gic);
}

static __BOOT__(void) MilkVDuoS__enter_kernel(uint64_t cpu)
{
    cpu_set_this_id(cpu);
    uint64_t stack_top = (uint64_t)&__k_stack_start[0] + STACK_SIZE;
    enter_kernel(stack_top, (uint64_t)&vector_table, (uint64_t)&CPU__enter_kernel, &milkv_duo_s_system_info);
}

__BOOT__(void) primary_booted(void)
{
    CPUID cpu = rd_mpidr() & 0xff;
    MilkVDuoS__init_system_info();
    MilkVDuoS__configure_uart1();
    BootUart__configure(&milkv_duo_s_system_info.trace_uart);
    BootUart__init();

    boot_debug_print(current_el);
    boot_debug_print_base((read_current_el() >> 2), 10);
    boot_debug_print(new_line);
    boot_debug_print(booting);
    boot_debug_print(new_line);

    boot_vectors_init();
    setup_boot_mappings(&milkv_duo_s_platform_info, &milkv_duo_s_system_info);
    enable_fp_simd();
    enable_tbi_for_kernel();
    enable_mmu();
    debug_boot_hardware_mappings(&milkv_duo_s_system_info);
    CPU__set_boot_id(cpu);
    _send('1');
    MilkVDuoS__enter_kernel(cpu);
}

__BOOT__(void) secondary_booted(uint64_t cpu)
{
    MilkVDuoS__enter_kernel(cpu);
}
