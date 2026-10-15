#include "uart.h"

#include <stddef.h>
#include <stdint.h>

#include "../../../kernel/kernel.h"
#include "../../../kernel/types/result.h"
#include "../../../kernel/types/optional.h"
#include "pl011.h"

#define NS16550_DATA 0U
#define NS16550_LINE_STATUS 5U
#define NS16550_DATA_READY (1U << 0)
#define NS16550_TRANSMIT_EMPTY (1U << 5)

static Address uart_base;
static UartKind uart_kind;
static uint64_t uart_register_shift;
static uint64_t uart_register_width;

void uart_use_info(UartInfoPtr info)
{
    uart_base = info->virtual_base;
    uart_kind = info->kind;
    uart_register_shift = info->register_shift;
    uart_register_width = info->register_width;
}

static uint32_t uart_read(uint64_t index)
{
    uint64_t offset = uart_kind == UART_KIND_NS16550 ? index << uart_register_shift : index;
    Address address = uart_base + offset;
    return uart_register_width == 1 ? *(volatile uint8_t *)address : *(volatile uint32_t *)address;
}

static void uart_write(uint64_t index, uint32_t value)
{
    uint64_t offset = uart_kind == UART_KIND_NS16550 ? index << uart_register_shift : index;
    Address address = uart_base + offset;
    if (uart_register_width == 1)
    {
        *(volatile uint8_t *)address = (uint8_t)value;
    }
    else
    {
        *(volatile uint32_t *)address = value;
    }
}

static bool uart_tx_ready()
{
    if (uart_kind == UART_KIND_NS16550)
    {
        return (uart_read(NS16550_LINE_STATUS) & NS16550_TRANSMIT_EMPTY) != 0;
    }
    return (uart_read(UART0_FR_OFFSET) & FR_TXFF) == 0;
}

static void _send(unsigned int c)
{
    const uint64_t max_wait = 100000;
    __asm__ volatile("dsb sy");
    for (uint64_t count = 0; !uart_tx_ready() && count < max_wait; count++)
    {
        CPU__noop();
    }

    if (uart_tx_ready())
    {
        uart_write(uart_kind == UART_KIND_NS16550 ? NS16550_DATA : UART0_DR_OFFSET, c);
    }
    __asm__ volatile("dsb sy");
}

// Very simple putchar that tries to write to common UART addresses
static void _putchar(char c)
{
    _send(c);
}

// Simple string output
void puts(const char * str)
{
    while (*str)
    {
        _putchar(*str);
        if (*str == '\n')
        {
            _putchar('\r'); // Add carriage return
        }
        str++;
    }
}

static volatile bool _check_read_ready()
{
    if (uart_kind == UART_KIND_NS16550)
    {
        return (uart_read(NS16550_LINE_STATUS) & NS16550_DATA_READY) == 0;
    }
    return (uart_read(UART0_FR_OFFSET) & FR_RXFE) != 0;
}

Optional(Char) _readchar()
{
    Optional(Char) res = None(Char);
    __asm__ volatile("dsb sy");
    WaitResult wait_res = Spin__wait(&_check_read_ready);
    if (wait_res == WAIT_OK)
    {
        res = Some(Char, (Char)(uart_read(uart_kind == UART_KIND_NS16550 ? NS16550_DATA : UART0_DR_OFFSET) & 0xFF));
    }

    __asm__ volatile("dsb sy");
    return res;
}

void echo(Char c)
{
    _send(c);
}

// Convert integer to string and print
void put_int(int num)
{
    if (num == 0)
    {
        _putchar('0');
        return;
    }

    if (num < 0)
    {
        _putchar('-');
        num = -num;
    }

    char digits[10];
    int count = 0;

    while (num > 0 && count < 10)
    {
        digits[count++] = '0' + (num % 10);
        num /= 10;
    }

    // Print in reverse order
    while (count > 0)
    {
        _putchar(digits[--count]);
    }
}

Result(InitCode) uart_init(UartInfoPtr info)
{
    uart_use_info(info);

    if (uart_kind == UART_KIND_PL011)
    {
        uart_write(UART0_CR_OFFSET, 0);
        uart_write(UART0_ICR_OFFSET, 0x7FF);
        uart_write(UART0_IBRD_OFFSET, 2);
        uart_write(UART0_FBRD_OFFSET, 0xB);
        uart_write(UART0_LCRH_OFFSET, 0b11 << 5);
        uart_write(UART0_CR_OFFSET, 0x301);
    }

    return Success(InitCode, ARCH_INIT_OK);
}
