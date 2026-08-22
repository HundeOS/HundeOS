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

#define QEMU_VIRT_GICD_PHYS_BASE 0x08000000UL
#define QEMU_VIRT_GICR_PHYS_BASE (QEMU_VIRT_GICD_PHYS_BASE + GICR_OFFSET)
#define QEMU_VIRT_GICD_BASE      ((uint64_t)&__gicd_start[0])
#define QEMU_VIRT_GICR_BASE      (QEMU_VIRT_GICD_BASE + GICR_OFFSET)
#define QEMU_VIRT_PCI_ECAM_PHYS_BASE 0x4010000000ULL
#define QEMU_VIRT_PCI_ECAM_SIZE      0x10000000ULL
#define QEMU_VIRT_PCI_BUS_START      0ULL
#define QEMU_VIRT_PCI_BUS_END        255ULL

__BOOT_DATA__(static BootPlatformInfo) qemu_virt_platform_info = {
    .cpu_count = 4,
    .memory_physical_start = 0,
    .memory_physical_end = 4ULL * 1024ULL * 1024ULL * 1024ULL,
    .uart_physical_base = BOOT_UART0_PHYS_BASE,
    .uart_virtual_base = BOOT_MMIO_VIRT_BASE,
    .uart_irq = BOOT_UART_IRQ_ID,
    .pci = Some(PciInfo, ((PciInfo) {
        .ecam_physical_base = QEMU_VIRT_PCI_ECAM_PHYS_BASE,
        .ecam_size = QEMU_VIRT_PCI_ECAM_SIZE,
        .bus_start = QEMU_VIRT_PCI_BUS_START,
        .bus_end = QEMU_VIRT_PCI_BUS_END,
    })),
    .sd_host = None(SdHostInfo),
    .interrupt_controller_kind = INTERRUPT_CONTROLLER_GIC_V3,
    .gic_distributor_physical_base = QEMU_VIRT_GICD_PHYS_BASE,
    .gic_distributor_virtual_base = QEMU_VIRT_GICD_BASE,
    .gic_redistributor_physical_base = QEMU_VIRT_GICR_PHYS_BASE,
    .gic_redistributor_virtual_base = QEMU_VIRT_GICR_BASE,
    .gic_cpu_interface_physical_base = 0,
    .gic_cpu_interface_virtual_base = 0,
};

__BOOT_DATA__(static SystemInfo) qemu_virt_system_info;

__BOOT_STRING__(daif_) = "DAIF=";
__BOOT_STRING__(pmr_) = "PMR=";
__BOOT_STRING__(igrp_) = "IGRPEN1=";

__BOOT__(SystemInfoPtr) qemu_virt_system_info_init(void)
{
    qemu_virt_system_info.cpu_count = qemu_virt_platform_info.cpu_count;

    qemu_virt_system_info.physical_memory.physical_start = qemu_virt_platform_info.memory_physical_start;
    qemu_virt_system_info.physical_memory.physical_end = qemu_virt_platform_info.memory_physical_end;
    qemu_virt_system_info.physical_memory.free_regions.len = 1;
    qemu_virt_system_info.physical_memory.free_regions.data[0].physical_start = (uint64_t)&__heap_start_phys[0];
    qemu_virt_system_info.physical_memory.free_regions.data[0].physical_end =
        qemu_virt_platform_info.memory_physical_end;

    qemu_virt_system_info.kernel_image.virtual_start = (uint64_t)&__text_virt;
    qemu_virt_system_info.kernel_image.physical_start = (uint64_t)&__text_phys;
    qemu_virt_system_info.kernel_image.physical_end = (uint64_t)&__lma_end;

    qemu_virt_system_info.kernel_heap.virtual_start = (uint64_t)&__heap_start[0];
    qemu_virt_system_info.kernel_heap.physical_start = (uint64_t)&__heap_start_phys[0];
    qemu_virt_system_info.kernel_heap.physical_end = qemu_virt_platform_info.memory_physical_end;

    qemu_virt_system_info.kernel_stack.virtual_start = (uint64_t)&__k_stack_start[0];
    qemu_virt_system_info.kernel_stack.physical_start = (uint64_t)&__k_stack_phys_start[0];
    qemu_virt_system_info.kernel_stack.physical_end = (uint64_t)&__k_stack_phys_end[0];

    qemu_virt_system_info.embedded_binaries.virtual_start = (uint64_t)&__embedded_binaries_start[0];
    qemu_virt_system_info.embedded_binaries.physical_start = (uint64_t)&__embedded_binaries_phys_start[0];
    qemu_virt_system_info.embedded_binaries.physical_end = (uint64_t)&__embedded_binaries_phys_end[0];

    qemu_virt_system_info.mmio.virtual_start = (uint64_t)&__mmio_start[0];
    qemu_virt_system_info.mmio.physical_start = qemu_virt_platform_info.uart_physical_base;
    qemu_virt_system_info.mmio.physical_end =
        qemu_virt_platform_info.uart_physical_base +
        ((uint64_t)&__mmio_end[0] - (uint64_t)&__mmio_start[0]);

    qemu_virt_system_info.uart.physical_base = qemu_virt_platform_info.uart_physical_base;
    qemu_virt_system_info.uart.virtual_base = qemu_virt_platform_info.uart_virtual_base;
    qemu_virt_system_info.uart.irq = qemu_virt_platform_info.uart_irq;
    qemu_virt_system_info.framebuffer = None(FramebufferInfo);
    qemu_virt_system_info.pci = qemu_virt_platform_info.pci;
    qemu_virt_system_info.sd_host = qemu_virt_platform_info.sd_host;

    qemu_virt_system_info.gic.distributor_physical_base = qemu_virt_platform_info.gic_distributor_physical_base;
    qemu_virt_system_info.gic.distributor_virtual_base = qemu_virt_platform_info.gic_distributor_virtual_base;
    qemu_virt_system_info.gic.redistributor_physical_base = qemu_virt_platform_info.gic_redistributor_physical_base;
    qemu_virt_system_info.gic.redistributor_virtual_base = qemu_virt_platform_info.gic_redistributor_virtual_base;

    qemu_virt_system_info.interrupt_controller.kind = qemu_virt_platform_info.interrupt_controller_kind;
    qemu_virt_system_info.interrupt_controller.distributor_physical_base =
        qemu_virt_platform_info.gic_distributor_physical_base;
    qemu_virt_system_info.interrupt_controller.distributor_virtual_base =
        qemu_virt_platform_info.gic_distributor_virtual_base;
    qemu_virt_system_info.interrupt_controller.redistributor_physical_base =
        qemu_virt_platform_info.gic_redistributor_physical_base;
    qemu_virt_system_info.interrupt_controller.redistributor_virtual_base =
        qemu_virt_platform_info.gic_redistributor_virtual_base;
    qemu_virt_system_info.interrupt_controller.cpu_interface_physical_base =
        qemu_virt_platform_info.gic_cpu_interface_physical_base;
    qemu_virt_system_info.interrupt_controller.cpu_interface_virtual_base =
        qemu_virt_platform_info.gic_cpu_interface_virtual_base;

    return &qemu_virt_system_info;
}

__BOOT__(void) qemu_virt_enter_kernel(uint64_t cpu)
{
    boot_debug_print(new_line);
    boot_debug_print(entering_hi_mem);
    boot_debug_print_base(cpu, 10);
    boot_debug_print(new_line);

    uint64_t stack_top = (uint64_t)&__k_stack_start[0] + ((cpu + 1) * STACK_SIZE);
    enter_kernel(stack_top, (uint64_t)&vector_table, (uint64_t)&CPU__enter_kernel, &qemu_virt_system_info);
}

__BOOT__(void) primary_booted(void)
{
    uint64_t daif, pmr, igrp;
    asm volatile("mrs %0, DAIF" : "=r"(daif));
    asm volatile("mrs %0, ICC_PMR_EL1" : "=r"(pmr));
    asm volatile("mrs %0, ICC_IGRPEN1_EL1" : "=r"(igrp));

    BootUart__set_physical_base(qemu_virt_platform_info.uart_physical_base);
    BootUart__init();
    qemu_virt_system_info_init();

    boot_debug_print(daif_);
    boot_debug_print_base(daif, 16);
    boot_debug_print(new_line);
    boot_debug_print(pmr_);
    boot_debug_print_base(pmr, 16);
    boot_debug_print(new_line);
    boot_debug_print(igrp_);
    boot_debug_print_base(igrp, 16);
    boot_debug_print(new_line);
    boot_debug_print(current_el);
    boot_debug_print_base((read_current_el() >> 2), 10);
    boot_debug_print(new_line);
    boot_debug_print(new_line);
    boot_debug_print(booting);
    boot_debug_print(new_line);

    boot_vectors_init();

    setup_boot_mappings(&qemu_virt_platform_info, &qemu_virt_system_info);
    enable_fp_simd();
    enable_tbi_for_kernel();
    enable_mmu();

    boot_secondary_cpus(qemu_virt_platform_info.cpu_count);

    qemu_virt_enter_kernel(1);
}

__BOOT__(void) secondary_booted(uint64_t cpu)
{
    boot_debug_print(new_line);
    boot_debug_print(secondary_booting);
    boot_debug_print_base(cpu, 10);
    boot_debug_print(new_line);
    boot_debug_print(new_line);

    boot_vectors_init();
    enable_fp_simd();
    enable_tbi_for_kernel();
    enable_mmu();

    qemu_virt_enter_kernel(cpu);
}
