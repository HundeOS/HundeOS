#include "uart.h"

#include <stddef.h>
#include <stdint.h>

#include "../../../kernel/kernel.h"
#include "../../../kernel/subsystems/io/io.h"
#include "../../../kernel/subsystems/io/console/console.h"
#include "../../../kernel/types/result.h"
#include "../../../kernel/types/optional.h"

static UartInfoPtr uart_info = NULL;

void uart_use_info(UartInfoPtr info)
{
    uart_info = info;
}

static volatile uint32_t * uart_reg(uint64_t offset)
{
    return (volatile uint32_t *)(uart_info->virtual_base + offset);
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
void puts(const char* str) {
    while (*str) {
        _putchar(*str);
        if (*str == '\n') {
            _putchar('\r');  // Add carriage return
        }
        str++;
    }
}

static volatile bool _check_read_ready()
{
    return (bool) (*uart_reg(UART0_FR_OFFSET) & FR_RXFE);
}

Optional(Char) _readchar()
{
    Optional(Char) res = None(Char);
    __asm__ volatile("dsb sy");
    WaitResult wait_res = Spin__wait(&_check_read_ready);
    if (wait_res == WAIT_OK)
    {
        res = Some(Char, (Char) (*uart_reg(UART0_DR_OFFSET) & 0xFF));
    }

    __asm__ volatile("dsb sy");
    return res;
}

void echo(Char c)
{
    _send(c);
}


// Convert integer to string and print
void put_int(int num) {
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
    *uart_reg(UART0_CR_OFFSET) = 0;           // turn off UART0

    *uart_reg(UART0_ICR_OFFSET) = 0x7FF;      // clear interrupts
    *uart_reg(UART0_IBRD_OFFSET) = 2;         // 115200 baud
    *uart_reg(UART0_FBRD_OFFSET) = 0xB;
    *uart_reg(UART0_LCRH_OFFSET) = 0b11 << 5; // 8n1
    *uart_reg(UART0_CR_OFFSET) = 0x301;       // enable Tx, Rx, FIFO

    return Success(InitCode, ARCH_INIT_OK);
}

Result(InitCode) uart_enable_irqs(UartInfoPtr info)
{
    uart_use_info(info);

    *uart_reg(UART0_CR_OFFSET) = 0;            // disable UART
    *uart_reg(UART0_ICR_OFFSET) = 0x7FF;       // clear all interrupts
    *uart_reg(UART0_IMSC_OFFSET) = I_RX | I_RT; // enable RX and receive-timeout interrupts
    *uart_reg(UART0_CR_OFFSET) = (1 << 9) | (1 << 8) | (1 << 0); // enable UART, TX, RX

    __d_println(IRQ2, "Vals");
    __d_printf(IRQ2,
        "IMSC=%x MIS=%x CR=%x\n",
        *uart_reg(UART0_IMSC_OFFSET),
        *uart_reg(UART0_MIS_OFFSET),
        *uart_reg(UART0_CR_OFFSET));

    return Success(InitCode, ARCH_INIT_OK);
}

ConsoleState console_state = (ConsoleState) {
    .state = NEW_IO_STATE,
    .write_buffer = EmptyRingBuffer(IOBuffer),
    .read_buffer = EmptyRingBuffer(IOBuffer),
    .parked_contexts = EmptyArray(ParkedContexts),
    .lock = MUTEX_INIT,
    .handle_counter = 0,
};

void uart_irq_handler(IOSubsystemPtr io_subsystem)
{
    __d_println(IRQ, "UART IRQ");
    uint32_t mis = *uart_reg(UART0_MIS_OFFSET);
    bool received_data = mis & (I_RX | I_RT);
    if (received_data)
    {
        // Drain RX FIFO
        while (!(*uart_reg(UART0_FR_OFFSET) & FR_RXFE))
        {
            Char ch = (Char) *uart_reg(UART0_DR_OFFSET) & 0xFF;
            __d_printf(IRQ, "Read char: %i\n", ch);
            __d_printf(IRQ, "Read char: %c\n", ch);
            IOBuffer__push(&console_state.read_buffer, ch);
        }
        // Clear RX + timeout
        *uart_reg(UART0_ICR_OFFSET) = I_RX | I_RT;
    }

    if (mis & I_TX)
    {
        // Fill TX FIFO until full or nothing left to send
        while (!(*uart_reg(UART0_FR_OFFSET) & FR_TXFF))
        {
            match_result(Char, IOBuffer__pop(&console_state.write_buffer),
                success(val)
                {
                    *uart_reg(UART0_DR_OFFSET) = val;
                },
                error(_code, _message) {}
            );

        }
        // Clear TX
        *uart_reg(UART0_ICR_OFFSET) = I_TX;
    }

    if (mis & I_ERR)
    {
        // Read & clear errors
        *uart_reg(UART0_RSR_ECR_OFFSET) = 0xFF;
        *uart_reg(UART0_ICR_OFFSET) = I_ERR;
    }
    if (received_data)
    {
        // only message io subsystem if there is a parked context
        match_optional(IOContextPtr, ParkedContexts__at(&console_state.parked_contexts, -1),
            some(parked_context)
            {
                __d_println(IOSUB, "Parked context found");
                IOSubsystem__data_arrived(io_subsystem, parked_context);
            },
            none() { __d_println(IOSUB, "NO parked context found");; }
        )
    }
}
