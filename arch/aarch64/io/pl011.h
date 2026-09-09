#ifndef __AARCH64_IO_PL011_H
#define __AARCH64_IO_PL011_H

#define UART0_DR_OFFSET 0x00
#define UART0_RSR_ECR_OFFSET 0x04
#define UART0_FR_OFFSET 0x18
#define UART0_IBRD_OFFSET 0x24
#define UART0_FBRD_OFFSET 0x28
#define UART0_LCRH_OFFSET 0x2C
#define UART0_CR_OFFSET 0x30
#define UART0_IFLS_OFFSET 0x34
#define UART0_IMSC_OFFSET 0x38
#define UART0_RIS_OFFSET 0x3C
#define UART0_MIS_OFFSET 0x40
#define UART0_ICR_OFFSET 0x44

#define FR_RXFE (1u << 4)
#define FR_TXFF (1u << 5)

#define DR_OE (1u << 11)
#define DR_BE (1u << 10)
#define DR_PE (1u << 9)
#define DR_FE (1u << 8)

#define UARTCR_UARTEN (1u << 0)
#define UARTCR_TXE (1u << 8)
#define UARTCR_RXE (1u << 9)

#define I_RX (1u << 4)
#define I_TX (1u << 5)
#define I_RT (1u << 6)
#define I_ERR ((1u << 7) | (1u << 8) | (1u << 9) | (1u << 10))

#endif
