#include <stdint.h>

#include "gpio.h"
#include <stdint.h>

#ifndef __IO_UART_H
#define __IO_UART_H

#ifdef BSP_VIRT
#define UART0_PHYS_BASE     0x09000000  // virt machine UART
#else
#define UART0_OFFSET    0x00201000
#define UART0_PHYS_BASE      (MMIO_BASE + UART0_OFFSET)  // 0x3F201000
#endif

extern const uint64_t __mmio_start[];

#define MIMO_VIRT_BASE (uintptr_t)&__mmio_start
#define MIMO_VIRT_END MIMO_VIRT_BASE + 4 * 1024 * 1024
#define UART0_BASE MIMO_VIRT_BASE + 0x0

#define P_UART0_DR        ((volatile unsigned int*)UART0_PHYS_BASE + 0x00)  // 0x3F201000 - Data Register
#define P_UART0_FR        ((volatile unsigned int*)UART0_PHYS_BASE + 0x18)  // 0x3F201018 - Flag Register
#define P_UART0_IBRD      ((volatile unsigned int*)UART0_PHYS_BASE + 0x24)  // 0x3F201024 - Integer Baud Rate
#define P_UART0_FBRD      ((volatile unsigned int*)UART0_PHYS_BASE + 0x28)  // 0x3F201028 - Fractional Baud Rate
#define P_UART0_LCRH      ((volatile unsigned int*)UART0_PHYS_BASE + 0x2C)  // 0x3F20102C - Line Control
#define P_UART0_CR        ((volatile unsigned int*)UART0_PHYS_BASE + 0x30)  // 0x3F201030 - Control Register
#define P_UART0_IMSC      ((volatile unsigned int*)UART0_PHYS_BASE + 0x38)  // 0x3F201038 - Interrupt Mask
#define P_UART0_ICR       ((volatile unsigned int*)UART0_PHYS_BASE + 0x44)  // 0x3F201044 - Interrupt Clear

#define UART0_DR        ((volatile unsigned int*)UART0_BASE + 0x00)  // 0x3F201000 - Data Register
#define UART0_FR        ((volatile unsigned int*)UART0_BASE + 0x18)  // 0x3F201018 - Flag Register
#define UART0_IBRD      ((volatile unsigned int*)UART0_BASE + 0x24)  // 0x3F201024 - Integer Baud Rate
#define UART0_FBRD      ((volatile unsigned int*)UART0_BASE + 0x28)  // 0x3F201028 - Fractional Baud Rate
#define UART0_LCRH      ((volatile unsigned int*)UART0_BASE + 0x2C)  // 0x3F20102C - Line Control
#define UART0_CR        ((volatile unsigned int*)UART0_BASE + 0x30)  // 0x3F201030 - Control Register
#define UART0_IMSC      ((volatile unsigned int*)UART0_BASE + 0x38)  // 0x3F201038 - Interrupt Mask
#define UART0_ICR       ((volatile unsigned int*)UART0_BASE + 0x44)  // 0x3F201044 - Interrupt Clear

void uart_init();

void put_int(int c);
void puts(const char *s);

#endif