#ifndef __ARCH_RISCV64_BOOT_QEMU_VIRT_PLATFORM_H
#define __ARCH_RISCV64_BOOT_QEMU_VIRT_PLATFORM_H

#include <stdint.h>

#include "device_tree.h"

#define RISCV_HIGHER_HALF_OFFSET 0xffffffc000000000ULL
#define RISCV_BOOTSTRAP_MEMORY_BASE 0x80000000ULL
#define RISCV_BOOTSTRAP_MEMORY_SIZE (1024ULL * 1024ULL * 1024ULL)
#define RISCV_BOOTSTRAP_UART_BASE 0x10000000ULL
#define RISCV_BOOTSTRAP_PLIC_BASE 0x0c000000ULL
#define RISCV_BOOTSTRAP_PLIC_SIZE 0x00600000ULL

typedef enum RiscVMmuStatus
{
    RISCV_MMU_OK = 0,
    RISCV_MMU_UNALIGNED_MEMORY,
    RISCV_MMU_MEMORY_TOO_LARGE,
    RISCV_MMU_UNSUPPORTED_MEMORY_BASE,
} RiscVMmuStatus;

void RiscVMmu__enable_bootstrap(void);
void RiscVMmu__activate_bootstrap(void);
RiscVMmuStatus RiscVMmu__validate(ConstRiscVPlatformInfoPtr platform);
uint64_t RiscVMmu__satp(void);

void RiscVTrap__install(void);
uint64_t RiscVTrap__last_cause(void);
uint64_t RiscVTrap__last_value(void);

void RiscVUart__put_char(char character);
void RiscVUart__puts(const char * text);
void RiscVUart__put_hex(uint64_t value);
void RiscVUart__use_base(uint64_t virtual_base);

void SBI__shutdown(void);

#endif
