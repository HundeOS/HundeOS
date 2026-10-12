#include <stddef.h>
#include <stdint.h>

#include "early_boot.h"
#include "all_defs.h"
#include "cpu.h"
#include "platform.h"
#include "platform_config.h"
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

typedef volatile uint8_t * RiscVUartRegister8Ptr;
typedef volatile uint32_t * RiscVUartRegister32Ptr;
typedef volatile uint32_t * RiscVGoldfishRtcRegistersPtr;
static uint64_t uart_virtual_base = RISCV_HIGHER_HALF_OFFSET + RISCV_BOOTSTRAP_UART_BASE;
static uint64_t uart_register_shift = RISCV_BOOTSTRAP_UART_REGISTER_SHIFT;
static uint64_t uart_register_width = RISCV_BOOTSTRAP_UART_REGISTER_WIDTH;
static KernelEarlyBootInfo kernel_early_boot_info;
CPUID RiscVBoot__stack_cpu_ids[MAX_CPUS];

static Optional(RealtimeClockSample) RiscVGoldfishRtc__sample(ConstRiscVPlatformInfoPtr platform)
{
    if (platform->rtc_size == 0 || platform->timebase_frequency == 0)
    {
        return None(RealtimeClockSample);
    }

    uint64_t before;
    uint64_t after;
    __asm__ volatile("rdtime %0" : "=r"(before)::"memory");
    RiscVGoldfishRtcRegistersPtr registers =
        (RiscVGoldfishRtcRegistersPtr)(RISCV_HIGHER_HALF_OFFSET + platform->rtc_base);
    uint64_t low = registers[0];
    uint64_t high = registers[1];
    __asm__ volatile("rdtime %0" : "=r"(after)::"memory");

    uint64_t unix_nanoseconds = (high << 32) | low;
    if (unix_nanoseconds == 0)
    {
        return None(RealtimeClockSample);
    }

    uint64_t counter = before + ((after - before) / 2U);
    uint64_t frequency = platform->timebase_frequency;
    uint64_t monotonic_usec = counter / frequency * 1000000ULL + counter % frequency * 1000000ULL / frequency;
    return Some(RealtimeClockSample, RealtimeClockSample__new(unix_nanoseconds / 1000ULL, monotonic_usec));
}

static uint32_t RiscVUart__read(uint64_t index)
{
    uint64_t address = uart_virtual_base + (index << uart_register_shift);
    return uart_register_width == 4 ? *(RiscVUartRegister32Ptr)address : *(RiscVUartRegister8Ptr)address;
}

static void RiscVUart__write(uint64_t index, uint8_t value)
{
    uint64_t address = uart_virtual_base + (index << uart_register_shift);
    if (uart_register_width == 4)
    {
        *(RiscVUartRegister32Ptr)address = value;
    }
    else
    {
        *(RiscVUartRegister8Ptr)address = value;
    }
}

void RiscVUart__configure(uint64_t virtual_base, uint64_t register_shift, uint64_t register_width)
{
    uart_virtual_base = virtual_base;
    uart_register_shift = register_shift;
    uart_register_width = register_width;
}

void RiscVUart__put_char(char character)
{
    while ((RiscVUart__read(UART_LINE_STATUS) & UART_TRANSMIT_EMPTY) == 0)
    {
    }
    RiscVUart__write(UART_TRANSMIT, (uint8_t)character);
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

static int64_t SBI__hart_start(uint64_t hart_id, uint64_t entry, uint64_t stack_index)
{
    register uint64_t arg0 __asm__("a0") = hart_id;
    register uint64_t arg1 __asm__("a1") = entry;
    register uint64_t arg2 __asm__("a2") = stack_index;
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
    uint64_t started_cpu_count = 1U;
    uint64_t next_stack_index = 2U;
    for (uint64_t hart_id = 0; hart_id < hart_limit; hart_id++)
    {
        if (hart_id == boot_hart_id)
        {
            continue;
        }

        uint64_t entry = (uint64_t)__riscv_secondary_entry_virtual - RISCV_HIGHER_HALF_OFFSET;
        RiscVBoot__stack_cpu_ids[next_stack_index - 1U] = hart_id;
        int64_t status = SBI__hart_start(hart_id, entry, next_stack_index);
        if (status == SBI_SUCCESS)
        {
            started_cpu_count++;
            next_stack_index++;
        }
        else
        {
            RiscVUart__puts("SBI hart start failed hart=");
            RiscVUart__put_hex(hart_id);
            RiscVUart__puts(" status=");
            RiscVUart__put_hex((uint64_t)status);
            RiscVUart__puts("\n");
        }
    }
    return started_cpu_count;
}

void RiscVBoot__secondary(uint64_t hart_id, uint64_t stack_index)
{
    CPU__assign_id(hart_id);
    CPU__enter_kernel(&kernel_early_boot_info.system_info);
    SBI__shutdown();
}

void RiscVBoot__main(uint64_t hart_id, uint64_t device_tree)
{
    RiscVDeviceTreeResult device_tree_result = RiscVDeviceTree__parse(device_tree);
    if (device_tree_result.status != RISCV_DEVICE_TREE_OK)
    {
        RiscVUart__puts("Device tree rejected status=");
        RiscVUart__put_hex(device_tree_result.status);
        RiscVUart__puts("\n");
        SBI__shutdown();
    }

    RiscVPlatformInfoPtr platform = &device_tree_result.platform;
    RiscVUart__configure(RISCV_HIGHER_HALF_OFFSET + platform->uart_base, platform->uart_register_shift,
                         platform->uart_register_width);

    RiscVMmuStatus mmu_status = RiscVMmu__validate(platform);
    if (mmu_status != RISCV_MMU_OK)
    {
        RiscVUart__puts("Sv39 bootstrap rejected status=");
        RiscVUart__put_hex(mmu_status);
        RiscVUart__puts("\n");
        SBI__shutdown();
    }
    RiscVTrap__install();

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
    RiscVBootPlatform__configure_system_info(&system_info);
    system_info.realtime_clock = RiscVGoldfishRtc__sample(platform);
    kernel_early_boot_info = KernelEarlyBootInfo__new(hart_id, system_info);
    KernelEarlyBootStatus handoff = KernelEarlyBoot__enter(&kernel_early_boot_info);
    if (handoff != KERNEL_EARLY_BOOT_READY)
    {
        RiscVUart__puts("Architecture-neutral early kernel handoff rejected status=");
        RiscVUart__put_hex(handoff);
        RiscVUart__puts("\n");
        SBI__shutdown();
    }
    CPU__set_boot_id(hart_id);
    RiscVBoot__stack_cpu_ids[0] = hart_id;
    kernel_early_boot_info.system_info.cpu_count =
        RiscVBoot__start_secondary_harts(hart_id, kernel_early_boot_info.system_info.cpu_count);
    CPU__assign_id(hart_id);
    CPU__enter_kernel(&kernel_early_boot_info.system_info);
    SBI__shutdown();
}
