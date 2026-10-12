#include <stdint.h>

#include "platform.h"
#include "platform_config.h"

#define RISCV_PAGE_SIZE 4096ULL
#define RISCV_PAGE_TABLE_ENTRIES 512ULL
#define RISCV_MEGAPAGE_SIZE (2ULL * 1024ULL * 1024ULL)

#define RISCV_PTE_VALID (1ULL << 0)
#define RISCV_PTE_READ (1ULL << 1)
#define RISCV_PTE_WRITE (1ULL << 2)
#define RISCV_PTE_EXECUTE (1ULL << 3)
#define RISCV_PTE_ACCESSED (1ULL << 6)
#define RISCV_PTE_DIRTY (1ULL << 7)
#define RISCV_SATP_MODE_SV39 (8ULL << 60)

#define RISCV_BOOT_TEXT __attribute__((section(".boot.text")))
#define RISCV_BOOT_PAGE_TABLE __attribute__((section(".boot.bss.page_tables"), aligned(RISCV_PAGE_SIZE)))

typedef uint64_t RiscVPageTable[RISCV_PAGE_TABLE_ENTRIES];

static RiscVPageTable root_page_table RISCV_BOOT_PAGE_TABLE;
static RiscVPageTable device_page_table_0 RISCV_BOOT_PAGE_TABLE;
static RiscVPageTable device_page_table_1 RISCV_BOOT_PAGE_TABLE;
static RiscVPageTable ram_page_table RISCV_BOOT_PAGE_TABLE;

static RISCV_BOOT_TEXT uint64_t RiscVMmu__page_table_entry(uint64_t physical_address)
{
    return (physical_address >> 2) | RISCV_PTE_VALID;
}

static RISCV_BOOT_TEXT uint64_t RiscVMmu__leaf_entry(uint64_t physical_address, uint64_t memory_attributes)
{
    return (physical_address >> 2) | RISCV_PTE_VALID | RISCV_PTE_READ | RISCV_PTE_WRITE | RISCV_PTE_EXECUTE |
           RISCV_PTE_ACCESSED | RISCV_PTE_DIRTY | memory_attributes;
}

static RISCV_BOOT_TEXT void RiscVMmu__map_device_range(uint64_t base, uint64_t size)
{
    if (size == 0)
    {
        return;
    }

    uint64_t first = base & ~(RISCV_MEGAPAGE_SIZE - 1U);
    uint64_t end = (base + size + RISCV_MEGAPAGE_SIZE - 1U) & ~(RISCV_MEGAPAGE_SIZE - 1U);
    for (uint64_t physical_address = first; physical_address < end; physical_address += RISCV_MEGAPAGE_SIZE)
    {
        uint64_t root_index = physical_address >> 30;
        uint64_t page_index = (physical_address >> 21) & 0x1ffU;
        if (root_index == 0)
        {
            device_page_table_0[page_index] =
                RiscVMmu__leaf_entry(physical_address, RISCV_PLATFORM_DEVICE_PTE_ATTRIBUTES);
        }
        else if (root_index == 1)
        {
            device_page_table_1[page_index] =
                RiscVMmu__leaf_entry(physical_address, RISCV_PLATFORM_DEVICE_PTE_ATTRIBUTES);
        }
    }
}

RISCV_BOOT_TEXT void RiscVMmu__activate_bootstrap(void)
{
    uint64_t satp = RISCV_SATP_MODE_SV39 | ((uint64_t)root_page_table >> 12);
    __asm__ volatile("sfence.vma zero, zero\n"
                     "csrw satp, %0\n"
                     "sfence.vma zero, zero"
                     :
                     : "r"(satp)
                     : "memory");
}

RISCV_BOOT_TEXT void RiscVMmu__enable_bootstrap(void)
{
    RiscVBootPlatform__prepare_hardware();
    RiscVMmu__map_device_range(RISCV_BOOTSTRAP_UART_BASE, RISCV_MEGAPAGE_SIZE);
    RiscVMmu__map_device_range(RISCV_BOOTSTRAP_RTC_BASE, RISCV_BOOTSTRAP_RTC_SIZE);
    RiscVMmu__map_device_range(RISCV_BOOTSTRAP_PLIC_BASE, RISCV_BOOTSTRAP_PLIC_SIZE);
    RiscVMmu__map_device_range(RISCV_BOOTSTRAP_SD_HOST_BASE, RISCV_BOOTSTRAP_SD_HOST_SIZE);

    uint64_t ram_megapages = RISCV_BOOTSTRAP_MEMORY_SIZE / RISCV_MEGAPAGE_SIZE;
    uint64_t first_ram_index = (RISCV_BOOTSTRAP_MEMORY_BASE >> 21) & 0x1ffU;
    for (uint64_t index = 0; index < ram_megapages; index++)
    {
        uint64_t physical_address = RISCV_BOOTSTRAP_MEMORY_BASE + (index * RISCV_MEGAPAGE_SIZE);
        ram_page_table[first_ram_index + index] =
            RiscVMmu__leaf_entry(physical_address, RISCV_PLATFORM_RAM_PTE_ATTRIBUTES);
    }

    root_page_table[0] = RiscVMmu__page_table_entry((uint64_t)device_page_table_0);
    root_page_table[1] = RiscVMmu__page_table_entry((uint64_t)device_page_table_1);
    uint64_t high_device_root_index = (RISCV_HIGHER_HALF_OFFSET >> 30) & 0x1ffU;
    root_page_table[high_device_root_index] = RiscVMmu__page_table_entry((uint64_t)device_page_table_0);
    root_page_table[high_device_root_index + 1U] = RiscVMmu__page_table_entry((uint64_t)device_page_table_1);

    uint64_t ram_root_index = (RISCV_BOOTSTRAP_MEMORY_BASE >> 30) & 0x1ffU;
    uint64_t high_ram_root_index = ((RISCV_HIGHER_HALF_OFFSET + RISCV_BOOTSTRAP_MEMORY_BASE) >> 30) & 0x1ffU;
    root_page_table[ram_root_index] = RiscVMmu__page_table_entry((uint64_t)ram_page_table);
    root_page_table[high_ram_root_index] = RiscVMmu__page_table_entry((uint64_t)ram_page_table);

    RiscVMmu__activate_bootstrap();
}

RiscVMmuStatus RiscVMmu__validate(ConstRiscVPlatformInfoPtr platform)
{
    if ((platform->memory_base & (RISCV_MEGAPAGE_SIZE - 1U)) != 0)
    {
        return RISCV_MMU_UNALIGNED_MEMORY;
    }
    if (platform->memory_base != RISCV_BOOTSTRAP_MEMORY_BASE)
    {
        return RISCV_MMU_UNSUPPORTED_MEMORY_BASE;
    }
    if (platform->memory_size > RISCV_BOOTSTRAP_MEMORY_SIZE)
    {
        return RISCV_MMU_MEMORY_TOO_LARGE;
    }
    return RISCV_MMU_OK;
}

uint64_t RiscVMmu__satp(void)
{
    uint64_t satp;
    __asm__ volatile("csrr %0, satp" : "=r"(satp));
    return satp;
}
