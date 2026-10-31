#include "../../kernel/arch/device.h"

typedef volatile uint32_t * DeviceRegisterPtr;

bool Arch__is_claimable_virtio_device(Address mapping)
{
    DeviceRegisterPtr registers = (DeviceRegisterPtr)mapping;
    return registers[0] == 0x74726976U && registers[1] >= 1 && (registers[2] == 2 || registers[2] == 18);
}

void Arch__reset_virtio_device(Address mapping, Address page_table)
{
    uint64_t previous_table;
    uint64_t previous_interrupts;
    __asm__ volatile("mrs %0, ttbr0_el1\n"
                     "mrs %1, daif\n"
                     "msr daifset, #2\n"
                     "msr ttbr0_el1, %2\n"
                     "dsb ish\n"
                     "tlbi vmalle1is\n"
                     "dsb ish\n"
                     "isb"
                     : "=&r"(previous_table), "=&r"(previous_interrupts)
                     : "r"(page_table)
                     : "memory");
    DeviceRegisterPtr registers = (DeviceRegisterPtr)mapping;
    registers[0x70 / sizeof(uint32_t)] = 0;
    __asm__ volatile("dsb sy" ::: "memory");
    // Do not reclaim DMA backing until the device has completed reset.
    while (registers[0x70 / sizeof(uint32_t)] != 0)
    {
        __asm__ volatile("yield" ::: "memory");
    }
    __asm__ volatile("dsb sy" ::: "memory");
    __asm__ volatile("msr ttbr0_el1, %0\n"
                     "dsb ish\n"
                     "tlbi vmalle1is\n"
                     "dsb ish\n"
                     "isb\n"
                     "msr daif, %1"
                     :
                     : "r"(previous_table), "r"(previous_interrupts)
                     : "memory");
}
