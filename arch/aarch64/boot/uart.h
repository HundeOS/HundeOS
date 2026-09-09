#ifndef __AARCH64_BOOT_UART_H
#define __AARCH64_BOOT_UART_H

#include <stdint.h>

#include "boot.h"
#include "../io/pl011.h"

#ifdef BSP_VIRT
#define BOOT_UART0_PHYS_BASE 0x09000000ULL
#define BOOT_UART_IRQ_ID 33ULL
#elif defined(BSP_RPI3)
#define BOOT_UART0_PHYS_BASE 0x3F201000ULL
#elif defined(BSP_RPI4)
#define BOOT_UART0_PHYS_BASE 0xFE201000ULL
#else
#error "No boot UART physical base configured for this BSP"
#endif

extern const uint64_t __mmio_start[];

#define BOOT_MMIO_VIRT_BASE ((uintptr_t)&__mmio_start[0])
#define BOOT_MMIO_VIRT_END (BOOT_MMIO_VIRT_BASE + (4 * 1024 * 1024))

__BOOT__(void) BootUart__set_physical_base(uint64_t physical_base);
__BOOT__(void) BootUart__init(void);
__BOOT__(volatile unsigned int *) BootUart__reg(uint64_t offset);

#define BOOT_UART0_REG(offset) BootUart__reg(offset)

#define BOOT_P_UART0_DR BOOT_UART0_REG(UART0_DR_OFFSET)
#define BOOT_P_UART0_FR BOOT_UART0_REG(UART0_FR_OFFSET)
#define BOOT_P_UART0_IBRD BOOT_UART0_REG(UART0_IBRD_OFFSET)
#define BOOT_P_UART0_FBRD BOOT_UART0_REG(UART0_FBRD_OFFSET)
#define BOOT_P_UART0_LCRH BOOT_UART0_REG(UART0_LCRH_OFFSET)
#define BOOT_P_UART0_CR BOOT_UART0_REG(UART0_CR_OFFSET)
#define BOOT_P_UART0_IMSC BOOT_UART0_REG(UART0_IMSC_OFFSET)
#define BOOT_P_UART0_ICR BOOT_UART0_REG(UART0_ICR_OFFSET)

#endif
