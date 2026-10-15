#ifndef __AARCH64_BOOT_UART_H
#define __AARCH64_BOOT_UART_H

#include <stdint.h>

#include "boot.h"
#include "../../../common/hardware/boot_info.h"

#ifdef BSP_VIRT
#define BOOT_UART0_PHYS_BASE 0x09000000ULL
#define BOOT_UART_IRQ_ID 33ULL
#elif defined(BSP_RPI3)
#define BOOT_UART0_PHYS_BASE 0x3F201000ULL
#elif defined(BSP_RPI4)
#define BOOT_UART0_PHYS_BASE 0xFE201000ULL
#elif defined(BSP_MILKV_DUO_S)
#define BOOT_UART0_PHYS_BASE 0x04140000ULL
#define BOOT_UART_IRQ_ID 60ULL
#else
#error "No boot UART physical base configured for this BSP"
#endif

extern const uint64_t __mmio_start[];

#define BOOT_MMIO_VIRT_BASE ((uintptr_t)&__mmio_start[0])
#define BOOT_MMIO_VIRT_END (BOOT_MMIO_VIRT_BASE + (4 * 1024 * 1024))

__BOOT__(void) BootUart__configure(UartInfoPtr info);
__BOOT__(void) BootUart__init(void);

#endif
