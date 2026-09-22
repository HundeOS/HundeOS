#include "../../kernel/arch/device.h"

#include <stdint.h>

#define RISCV_SATP_MODE_SV39 (8ULL << 60U)
#define RISCV_SATP_PPN_MASK ((1ULL << 44U) - 1U)
#define RISCV_SSTATUS_SIE (1ULL << 1U)
#define VIRTIO_MAGIC 0x74726976U
#define VIRTIO_DEVICE_BLOCK 2U
#define VIRTIO_STATUS_OFFSET 0x70U

typedef volatile uint32_t * DeviceRegisterPtr;

static uint64_t RiscVDevice__satp_for_root(Address physical_address)
{
    return RISCV_SATP_MODE_SV39 | ((physical_address >> 12U) & RISCV_SATP_PPN_MASK);
}

bool Arch__is_virtio_block_device(Address mapping)
{
    DeviceRegisterPtr registers = (DeviceRegisterPtr)mapping;
    return registers[0] == VIRTIO_MAGIC && registers[1] >= 1 && registers[2] == VIRTIO_DEVICE_BLOCK;
}

void Arch__reset_virtio_device(Address mapping, Address page_table)
{
    uint64_t previous_satp;
    uint64_t previous_status;
    __asm__ volatile("csrr %0, satp\n"
                     "csrr %1, sstatus\n"
                     "csrc sstatus, %2\n"
                     "csrw satp, %3\n"
                     "sfence.vma zero, zero"
                     : "=&r"(previous_satp), "=&r"(previous_status)
                     : "r"(RISCV_SSTATUS_SIE), "r"(RiscVDevice__satp_for_root(page_table))
                     : "memory");

    DeviceRegisterPtr registers = (DeviceRegisterPtr)mapping;
    registers[VIRTIO_STATUS_OFFSET / sizeof(uint32_t)] = 0;
    __asm__ volatile("fence iorw, iorw" ::: "memory");
    while (registers[VIRTIO_STATUS_OFFSET / sizeof(uint32_t)] != 0)
    {
        __asm__ volatile("nop");
    }

    __asm__ volatile("csrw satp, %0\n"
                     "sfence.vma zero, zero\n"
                     "csrw sstatus, %1"
                     :
                     : "r"(previous_satp), "r"(previous_status)
                     : "memory");
}
