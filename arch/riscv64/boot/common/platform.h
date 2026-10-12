#ifndef __ARCH_RISCV64_BOOT_COMMON_PLATFORM_H
#define __ARCH_RISCV64_BOOT_COMMON_PLATFORM_H

#include <stdint.h>

#include "device_tree.h"
#include "system_info.h"

#define RISCV_HIGHER_HALF_OFFSET 0xffffffc000000000ULL

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

void RiscVBootPlatform__prepare_hardware(void);
void RiscVBootPlatform__configure_system_info(SystemInfoPtr info);

void RiscVTrap__install(void);

void RiscVUart__put_char(char character);
void RiscVUart__puts(const char * text);
void RiscVUart__put_hex(uint64_t value);
void RiscVUart__configure(uint64_t virtual_base, uint64_t register_shift, uint64_t register_width);

void SBI__shutdown(void);

#endif
