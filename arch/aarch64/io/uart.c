#include "uart.h"

#include <stddef.h>
#include <stdint.h>

#include "../../../kernel/kernel.h"
#include "../../../kernel/types/result.h"
#include "../../../kernel/types/optional.h"

static Address uart_base;

void uart_use_info(UartInfoPtr info)
{
    uart_base = info->virtual_base;
}

static volatile uint32_t * uart_reg(uint64_t offset)
{
    return (volatile uint32_t *)(uart_base + offset);
}

static bool uart_tx_ready()
{
    return (*uart_reg(UART0_FR_OFFSET) & FR_TXFF) == 0;
}

static void _send(unsigned int c)
{
    const uint64_t max_wait = 100000;
    __asm__ volatile("dsb sy");
    for (uint64_t count = 0; !uart_tx_ready() && count < max_wait; count++)
    {
        CPU__wait();
    }

    if (uart_tx_ready())
    {
        *uart_reg(UART0_DR_OFFSET) = c;
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
    return (bool)(*uart_reg(UART0_FR_OFFSET) & FR_RXFE);
}

Optional(Char) _readchar()
{
    Optional(Char) res = None(Char);
    __asm__ volatile("dsb sy");
    WaitResult wait_res = Spin__wait(&_check_read_ready);
    if (wait_res == WAIT_OK)
    {
        res = Some(Char, (Char)(*uart_reg(UART0_DR_OFFSET) & 0xFF));
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

    // initialize UART
    *uart_reg(UART0_CR_OFFSET) = 0; // turn off UART0

    *uart_reg(UART0_ICR_OFFSET) = 0x7FF; // clear interrupts
    *uart_reg(UART0_IBRD_OFFSET) = 2;    // 115200 baud
    *uart_reg(UART0_FBRD_OFFSET) = 0xB;
    *uart_reg(UART0_LCRH_OFFSET) = 0b11 << 5; // 8n1
    *uart_reg(UART0_CR_OFFSET) = 0x301;       // enable Tx, Rx, FIFO

    return Success(InitCode, ARCH_INIT_OK);
}
