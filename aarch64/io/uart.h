#include "gpio.h"
#include <stdint.h>

#ifndef __IO_UART_H
#define __IO_UART_H

#ifdef BSP_VIRT
#define UART0_BASE     0x09000000  // virt machine UART
#else
#define UART0_OFFSET    0x00201000
#define UART0_BASE      (MMIO_BASE + UART0_OFFSET)  // 0x3F201000
#endif

#define UART0_DR        ((volatile unsigned int*)UART0_BASE + 0x00)  // 0x3F201000 - Data Register
#define UART0_FR        ((volatile unsigned int*)UART0_BASE + 0x18)  // 0x3F201018 - Flag Register
#define UART0_IBRD      ((volatile unsigned int*)UART0_BASE + 0x24)  // 0x3F201024 - Integer Baud Rate
#define UART0_FBRD      ((volatile unsigned int*)UART0_BASE + 0x28)  // 0x3F201028 - Fractional Baud Rate
#define UART0_LCRH      ((volatile unsigned int*)UART0_BASE + 0x2C)  // 0x3F20102C - Line Control
#define UART0_CR        ((volatile unsigned int*)UART0_BASE + 0x30)  // 0x3F201030 - Control Register
#define UART0_IMSC      ((volatile unsigned int*)UART0_BASE + 0x38)  // 0x3F201038 - Interrupt Mask
#define UART0_ICR       ((volatile unsigned int*)UART0_BASE + 0x44)  // 0x3F201044 - Interrupt Clear

void uart_init();

#endif