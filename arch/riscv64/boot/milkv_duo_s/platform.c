#include <stdint.h>

#include "../common/platform.h"
#include "platform_config.h"

#define RISCV_BOOT_TEXT __attribute__((section(".boot.text")))

#define DUO_S_PINMUX_BASE 0x03001000ULL
#define DUO_S_PINMUX_UART1_TX_OFFSET 0x64ULL
#define DUO_S_PINMUX_UART1_RX_OFFSET 0x68ULL
#define DUO_S_PINMUX_UART1_FUNCTION 6U

#define DUO_S_UART1_BASE 0x04150000ULL
#define DUO_S_UART1_SIZE 0x1000ULL
#define DUO_S_UART1_IRQ 45ULL
#define DUO_S_UART_CLOCK_HZ 25000000ULL
#define DUO_S_UART_BAUD 115200ULL

#define UART_RECEIVE_BUFFER 0ULL
#define UART_INTERRUPT_ENABLE 1ULL
#define UART_FIFO_CONTROL 2ULL
#define UART_LINE_CONTROL 3ULL
#define UART_MODEM_CONTROL 4ULL
#define UART_DIVISOR_FRACTION_OFFSET 0xc0ULL
#define UART_LINE_CONTROL_8N1 0x03U
#define UART_LINE_CONTROL_DLAB 0x80U
#define UART_FIFO_ENABLE_AND_CLEAR 0x07U
#define UART_MODEM_DTR_RTS 0x03U

typedef volatile uint32_t * RiscVBootRegisterPtr;

static RISCV_BOOT_TEXT void RiscVBootPlatform__write32(uint64_t address, uint32_t value)
{
    *(RiscVBootRegisterPtr)address = value;
}

static RISCV_BOOT_TEXT uint32_t RiscVBootPlatform__read32(uint64_t address)
{
    return *(RiscVBootRegisterPtr)address;
}

static RISCV_BOOT_TEXT uint64_t RiscVBootPlatform__uart_register(uint64_t index)
{
    return DUO_S_UART1_BASE + (index << 2U);
}

static RISCV_BOOT_TEXT void RiscVBootPlatform__configure_uart1(void)
{
    uint64_t baud_clock = DUO_S_UART_BAUD * 16ULL;
    uint32_t integer_divisor = (uint32_t)(DUO_S_UART_CLOCK_HZ / baud_clock);
    uint64_t remainder = DUO_S_UART_CLOCK_HZ % baud_clock;

    RiscVBootPlatform__write32(RiscVBootPlatform__uart_register(UART_INTERRUPT_ENABLE), 0);
    RiscVBootPlatform__write32(DUO_S_UART1_BASE + UART_DIVISOR_FRACTION_OFFSET, UINT32_MAX);
    uint32_t fraction_mask = RiscVBootPlatform__read32(DUO_S_UART1_BASE + UART_DIVISOR_FRACTION_OFFSET);
    RiscVBootPlatform__write32(DUO_S_UART1_BASE + UART_DIVISOR_FRACTION_OFFSET, 0);

    uint32_t fraction = 0;
    if (fraction_mask != 0)
    {
        uint64_t fraction_scale = (uint64_t)fraction_mask + 1ULL;
        fraction = (uint32_t)((remainder * fraction_scale + baud_clock / 2ULL) / baud_clock);
        if ((uint64_t)fraction == fraction_scale)
        {
            integer_divisor++;
            fraction = 0;
        }
    }

    RiscVBootPlatform__write32(RiscVBootPlatform__uart_register(UART_LINE_CONTROL), UART_LINE_CONTROL_DLAB);
    RiscVBootPlatform__write32(RiscVBootPlatform__uart_register(UART_RECEIVE_BUFFER), integer_divisor & 0xffU);
    RiscVBootPlatform__write32(RiscVBootPlatform__uart_register(UART_INTERRUPT_ENABLE),
                               (integer_divisor >> 8U) & 0xffU);
    RiscVBootPlatform__write32(DUO_S_UART1_BASE + UART_DIVISOR_FRACTION_OFFSET, fraction);
    RiscVBootPlatform__write32(RiscVBootPlatform__uart_register(UART_LINE_CONTROL), UART_LINE_CONTROL_8N1);
    RiscVBootPlatform__write32(RiscVBootPlatform__uart_register(UART_FIFO_CONTROL), UART_FIFO_ENABLE_AND_CLEAR);
    RiscVBootPlatform__write32(RiscVBootPlatform__uart_register(UART_MODEM_CONTROL), UART_MODEM_DTR_RTS);
}

RISCV_BOOT_TEXT void RiscVBootPlatform__prepare_hardware(void)
{
    if (!RISCV_PLATFORM_CONFIGURE_UART1)
    {
        return;
    }

    RiscVBootPlatform__write32(DUO_S_PINMUX_BASE + DUO_S_PINMUX_UART1_TX_OFFSET, DUO_S_PINMUX_UART1_FUNCTION);
    RiscVBootPlatform__write32(DUO_S_PINMUX_BASE + DUO_S_PINMUX_UART1_RX_OFFSET, DUO_S_PINMUX_UART1_FUNCTION);
    RiscVBootPlatform__configure_uart1();
    __asm__ volatile("fence iorw, iorw" ::: "memory");
}

void RiscVBootPlatform__configure_system_info(SystemInfoPtr info)
{
    info->default_user_stack_size = 2ULL * 1024ULL * 1024ULL;
    if (!RISCV_PLATFORM_KERNEL_TRACE)
    {
        info->trace_uart = UartInfo__new(0, 0, 0, 0, UART_KIND_NS16550, 0, 0);
    }
    else if (RISCV_PLATFORM_CONFIGURE_UART1)
    {
        info->trace_uart = UartInfo__new(DUO_S_UART1_BASE, RISCV_HIGHER_HALF_OFFSET + DUO_S_UART1_BASE,
                                         DUO_S_UART1_SIZE, DUO_S_UART1_IRQ, UART_KIND_NS16550, 2, 4);
    }
    info->console_uart =
        UartInfo__new(RISCV_PLATFORM_CONSOLE_UART_BASE, RISCV_HIGHER_HALF_OFFSET + RISCV_PLATFORM_CONSOLE_UART_BASE,
                      DUO_S_UART1_SIZE, RISCV_PLATFORM_CONSOLE_UART_IRQ, UART_KIND_NS16550, 2, 4);
}
