#ifndef __IO_UART_H
#define __IO_UART_H

#include <stdint.h>
#include "../../../kernel/subsystems/io/io.h"
#include "../../../kernel/types/result.h"
#include "../../../kernel/arch/init.h"
#include "../gicr.h"

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

#define P_UART0_DR        ((volatile unsigned int*)(UART0_PHYS_BASE + 0x00))  // 0x3F201000 - Data Register
#define P_UART0_FR        ((volatile unsigned int*)(UART0_PHYS_BASE + 0x18))  // 0x3F201018 - Flag Register
#define P_UART0_IBRD      ((volatile unsigned int*)(UART0_PHYS_BASE + 0x24))  // 0x3F201024 - Integer Baud Rate
#define P_UART0_FBRD      ((volatile unsigned int*)(UART0_PHYS_BASE + 0x28))  // 0x3F201028 - Fractional Baud Rate
#define P_UART0_LCRH      ((volatile unsigned int*)(UART0_PHYS_BASE + 0x2C))  // 0x3F20102C - Line Control
#define P_UART0_CR        ((volatile unsigned int*)(UART0_PHYS_BASE + 0x30))  // 0x3F201030 - Control Register
#define P_UART0_IMSC      ((volatile unsigned int*)(UART0_PHYS_BASE + 0x38))  // 0x3F201038 - Interrupt Mask
#define P_UART0_ICR       ((volatile unsigned int*)(UART0_PHYS_BASE + 0x44))  // 0x3F201044 - Interrupt Clear

#define UART0_DR        ((volatile unsigned int*)(UART0_BASE + 0x00))  // 0x3F201000 - Data Register
#define UART0_FR        ((volatile unsigned int*)(UART0_BASE + 0x18))  // 0x3F201018 - Flag Register
#define UART0_IBRD      ((volatile unsigned int*)(UART0_BASE + 0x24))  // 0x3F201024 - Integer Baud Rate
#define UART0_FBRD      ((volatile unsigned int*)(UART0_BASE + 0x28))  // 0x3F201028 - Fractional Baud Rate
#define UART0_LCRH      ((volatile unsigned int*)(UART0_BASE + 0x2C))  // 0x3F20102C - Line Control
#define UART0_CR        ((volatile unsigned int*)(UART0_BASE + 0x30))  // 0x3F201030 - Control Register
#define UART0_IMSC      ((volatile unsigned int*)(UART0_BASE + 0x38))  // 0x3F201038 - Interrupt Mask
#define UART0_ICR       ((volatile unsigned int*)(UART0_BASE + 0x44))  // 0x3F201044 - Interrupt Clear

#define FR_RXFE      (1u << 4)
#define DR_OE        (1u << 11)
#define DR_BE        (1u << 10)
#define DR_PE        (1u << 9)
#define DR_FE        (1u << 8)

#define UARTCR_UARTEN   (1u << 0)
#define UARTCR_TXE      (1u << 8)
#define UARTCR_RXE      (1u << 9)

#define UART0_RSR_ECR   (*(volatile uint32_t *)(UART0_BASE + 0x004))
#define UARTIMSC    (*(volatile uint32_t *)(UART0_BASE + 0x038))
#define UARTMIS     (*(volatile uint32_t *)(UART0_BASE + 0x040))
#define UARTICR     (*(volatile uint32_t *)(UART0_BASE + 0x044))
#define UARTIFLS    (*(volatile uint32_t *)(UART0_BASE + 0x034))
#define UARTRIS     (*(volatile uint32_t *)(UART0_BASE + 0x03C))

#define UART_INT 33

#define FR_RXFE (1u << 4)
#define FR_TXFF (1u << 5)
#define I_RX    (1u << 4)
#define I_TX    (1u << 5)
#define I_RT    (1u << 6)
#define I_ERR   ((1u<<7)|(1u<<8)|(1u<<9)|(1u<<10))

#define UART_IRQ_ID 33  // PL011 UART on QEMU virt


Result(InitCode) uart_init();
Result(InitCode) uart_enable_irqs();

void put_int(int c);
void puts(const char *s);

void uart_irq_handler();

// Result(ByteCount) read_line(BufferPtr buffer);

#endif