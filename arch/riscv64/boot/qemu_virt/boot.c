#include <stddef.h>
#include <stdint.h>

#include "early_boot.h"
#include "all_defs.h"
#include "cpu.h"
#include "platform.h"
#include "system_info.h"
#include "../../cpu.h"

#define UART_TRANSMIT 0ULL
#define UART_LINE_STATUS 5ULL
#define UART_TRANSMIT_EMPTY (1U << 5)

#define SBI_SYSTEM_RESET_EXTENSION 0x53525354ULL
#define SBI_SYSTEM_RESET_FUNCTION 0ULL
#define SBI_RESET_TYPE_SHUTDOWN 0ULL
#define SBI_RESET_REASON_NONE 0ULL
#define SBI_HSM_EXTENSION 0x48534dULL
#define SBI_HART_START_FUNCTION 0ULL
#define SBI_SUCCESS 0LL
#define RISCV_PAGE_SIZE 4096ULL

extern const uint8_t __image_virtual_start[];
extern const uint8_t __image_virtual_end[];
extern const uint8_t __embedded_binaries_start[];
extern const uint8_t __embedded_binaries_end[];
extern const uint8_t __boot_stack_virtual_start[];
extern const uint8_t __boot_stack_virtual_top[];
extern const uint8_t __riscv_secondary_entry_virtual[];

typedef volatile uint8_t * RiscVUartRegistersPtr;
static uint64_t uart_virtual_base = RISCV_HIGHER_HALF_OFFSET + RISCV_BOOTSTRAP_UART_BASE;
static KernelEarlyBootInfo kernel_early_boot_info;

static RiscVUartRegistersPtr RiscVUart__registers(void)
{
    return (RiscVUartRegistersPtr)uart_virtual_base;
}

void RiscVUart__use_base(uint64_t virtual_base)
{
    uart_virtual_base = virtual_base;
}

void RiscVUart__put_char(char character)
{
    RiscVUartRegistersPtr registers = RiscVUart__registers();
    while ((registers[UART_LINE_STATUS] & UART_TRANSMIT_EMPTY) == 0)
    {
    }
    registers[UART_TRANSMIT] = (uint8_t)character;
}

void RiscVUart__puts(const char * text)
{
    while (*text != '\0')
    {
        if (*text == '\n')
        {
            RiscVUart__put_char('\r');
        }
        RiscVUart__put_char(*text);
        text++;
    }
}

void RiscVUart__put_hex(uint64_t value)
{
    static const char digits[] = "0123456789abcdef";
    RiscVUart__puts("0x");
    for (int shift = 60; shift >= 0; shift -= 4)
    {
        RiscVUart__put_char(digits[(value >> shift) & 0xf]);
    }
}

void SBI__shutdown(void)
{
    register uint64_t arg0 __asm__("a0") = SBI_RESET_TYPE_SHUTDOWN;
    register uint64_t arg1 __asm__("a1") = SBI_RESET_REASON_NONE;
    register uint64_t function __asm__("a6") = SBI_SYSTEM_RESET_FUNCTION;
    register uint64_t extension __asm__("a7") = SBI_SYSTEM_RESET_EXTENSION;
    __asm__ volatile("ecall" : "+r"(arg0), "+r"(arg1) : "r"(function), "r"(extension) : "memory");
}

static int64_t SBI__hart_start(uint64_t hart_id, uint64_t entry, uint64_t logical_id)
{
    register uint64_t arg0 __asm__("a0") = hart_id;
    register uint64_t arg1 __asm__("a1") = entry;
    register uint64_t arg2 __asm__("a2") = logical_id;
    register uint64_t function __asm__("a6") = SBI_HART_START_FUNCTION;
    register uint64_t extension __asm__("a7") = SBI_HSM_EXTENSION;
    __asm__ volatile("fence rw, rw\n"
                     "ecall"
                     : "+r"(arg0)
                     : "r"(arg1), "r"(arg2), "r"(function), "r"(extension)
                     : "memory");
    return (int64_t)arg0;
}

static uint64_t RiscVBoot__start_secondary_harts(uint64_t boot_hart_id, uint64_t cpu_count)
{
    uint64_t hart_limit = cpu_count < MAX_CPUS ? cpu_count : MAX_CPUS;
    uint64_t next_logical_id = 2U;
    for (uint64_t hart_id = 0; hart_id < hart_limit; hart_id++)
    {
        if (hart_id == boot_hart_id)
        {
            continue;
        }

        uint64_t entry = (uint64_t)__riscv_secondary_entry_virtual - RISCV_HIGHER_HALF_OFFSET;
        int64_t status = SBI__hart_start(hart_id, entry, next_logical_id);
        RiscVUart__puts("SBI hart start physical=");
        RiscVUart__put_hex(hart_id);
        RiscVUart__puts(" logical=");
        RiscVUart__put_hex(next_logical_id);
        RiscVUart__puts(" status=");
        RiscVUart__put_hex((uint64_t)status);
        RiscVUart__puts("\n");
        if (status == SBI_SUCCESS)
        {
            next_logical_id++;
        }
    }
    return next_logical_id - 1U;
}

void RiscVBoot__secondary(uint64_t hart_id, uint64_t logical_id)
{
    RiscVCPU__assign_hart(logical_id, hart_id);
    CPU__enter_kernel(&kernel_early_boot_info.system_info);
    SBI__shutdown();
}

void RiscVBoot__main(uint64_t hart_id, uint64_t device_tree)
{
    RiscVUart__puts("hund RISC-V bring-up\n");
    RiscVUart__puts("higher-half entry=");
    RiscVUart__put_hex((uint64_t)RiscVBoot__main);
    RiscVUart__puts("\n");
    RiscVUart__puts("hart=");
    RiscVUart__put_hex(hart_id);
    RiscVUart__puts(" dtb=");
    RiscVUart__put_hex(device_tree);
    RiscVUart__puts("\nS-mode entry and UART are working\n");

    RiscVDeviceTreeResult device_tree_result = RiscVDeviceTree__parse(device_tree);
    if (device_tree_result.status != RISCV_DEVICE_TREE_OK)
    {
        RiscVUart__puts("Device tree rejected status=");
        RiscVUart__put_hex(device_tree_result.status);
        RiscVUart__puts("\n");
        SBI__shutdown();
    }

    RiscVPlatformInfoPtr platform = &device_tree_result.platform;
    RiscVUart__use_base(RISCV_HIGHER_HALF_OFFSET + platform->uart_base);
    RiscVUart__puts("DTB memory=");
    RiscVUart__put_hex(platform->memory_base);
    RiscVUart__puts(" size=");
    RiscVUart__put_hex(platform->memory_size);
    RiscVUart__puts(" cpus=");
    RiscVUart__put_hex(platform->cpu_count);
    RiscVUart__puts(" timebase=");
    RiscVUart__put_hex(platform->timebase_frequency);
    RiscVUart__puts("\nDTB uart=");
    RiscVUart__put_hex(platform->uart_base);
    RiscVUart__puts(" irq=");
    RiscVUart__put_hex(platform->uart_irq);
    RiscVUart__puts(" plic=");
    RiscVUart__put_hex(platform->plic_base);
    RiscVUart__puts(" sources=");
    RiscVUart__put_hex(platform->plic_source_count);
    RiscVUart__puts(" virtio=");
    RiscVUart__put_hex(platform->virtio_mmio_count);
    RiscVUart__puts(" pci=");
    RiscVUart__put_hex(platform->pci_ecam_base);
    RiscVUart__puts(" reserved=");
    RiscVUart__put_hex(PhysicalMemoryRegions__len(&platform->reserved_memory_regions));
    RiscVUart__puts("\n");

    RiscVMmuStatus mmu_status = RiscVMmu__validate(platform);
    if (mmu_status != RISCV_MMU_OK)
    {
        RiscVUart__puts("Sv39 bootstrap rejected status=");
        RiscVUart__put_hex(mmu_status);
        RiscVUart__puts("\n");
        SBI__shutdown();
    }
    RiscVUart__puts("Sv39 enabled satp=");
    RiscVUart__put_hex(RiscVMmu__satp());
    RiscVUart__puts("\n");

    RiscVTrap__install();
    __asm__ volatile("ebreak");
    RiscVUart__puts("S-mode trap returned cause=");
    RiscVUart__put_hex(RiscVTrap__last_cause());
    RiscVUart__puts(" stval=");
    RiscVUart__put_hex(RiscVTrap__last_value());
    RiscVUart__puts("\n");

    uint64_t image_virtual_start = (uint64_t)__image_virtual_start;
    uint64_t image_virtual_end = (uint64_t)__image_virtual_end;
    uint64_t image_physical_start = image_virtual_start - RISCV_HIGHER_HALF_OFFSET;
    uint64_t image_physical_end = image_virtual_end - RISCV_HIGHER_HALF_OFFSET;
    uint64_t embedded_binaries_virtual_start = (uint64_t)__embedded_binaries_start;
    uint64_t embedded_binaries_virtual_end = (uint64_t)__embedded_binaries_end;
    uint64_t heap_physical_start = (image_physical_end + RISCV_PAGE_SIZE - 1U) & ~(RISCV_PAGE_SIZE - 1U);
    uint64_t memory_end = platform->memory_base + platform->memory_size;
    RiscVKernelLayout layout = RiscVKernelLayout__new(
        MemoryRegion__new(image_virtual_start, image_physical_start, image_physical_end),
        MemoryRegion__new(embedded_binaries_virtual_start, embedded_binaries_virtual_start - RISCV_HIGHER_HALF_OFFSET,
                          embedded_binaries_virtual_end - RISCV_HIGHER_HALF_OFFSET),
        MemoryRegion__new(RISCV_HIGHER_HALF_OFFSET + heap_physical_start, heap_physical_start, memory_end),
        MemoryRegion__new((uint64_t)__boot_stack_virtual_start,
                          (uint64_t)__boot_stack_virtual_start - RISCV_HIGHER_HALF_OFFSET,
                          (uint64_t)__boot_stack_virtual_top - RISCV_HIGHER_HALF_OFFSET));
    SystemInfo system_info = RiscVPlatformInfo__system_info(platform, &layout, hart_id);
    RiscVUart__puts("SystemInfo free regions=");
    RiscVUart__put_hex(PhysicalMemoryRegions__len(&system_info.physical_memory.free_regions));
    RiscVUart__puts("\n");
    kernel_early_boot_info = KernelEarlyBootInfo__new(hart_id, system_info);
    KernelEarlyBootStatus handoff = KernelEarlyBoot__enter(&kernel_early_boot_info);
    if (handoff != KERNEL_EARLY_BOOT_READY)
    {
        RiscVUart__puts("Architecture-neutral early kernel handoff rejected status=");
        RiscVUart__put_hex(handoff);
        RiscVUart__puts("\n");
        SBI__shutdown();
    }

    RiscVUart__puts("Architecture-neutral early kernel handoff accepted\n");
    kernel_early_boot_info.system_info.cpu_count =
        RiscVBoot__start_secondary_harts(hart_id, kernel_early_boot_info.system_info.cpu_count);
    RiscVCPU__assign_hart(1U, hart_id);
    CPU__enter_kernel(&kernel_early_boot_info.system_info);
    SBI__shutdown();
}
