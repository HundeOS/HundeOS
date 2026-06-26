#include "uart.h"

#include <stdint.h>

#include "../../../kernel/kernel.h"
#include "../../../kernel/subsystems/io/io.h"
#include "../../../kernel/subsystems/io/console/console.h"
#include "../../../kernel/types/result.h"
#include "../../../kernel/types/optional.h"


static volatile bool _check_ready()
{
    return (bool) (*UART0_FR & 0x20);
}

static void _send(unsigned int c)
{
    __asm__ volatile("dsb sy");
    Spin__wait(&_check_ready);
    *UART0_DR = c;
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
    return (bool) (*UART0_FR & FR_RXFE);
}

Optional(Char) _readchar()
{
    Optional(Char) res = None(Char);
    __asm__ volatile("dsb sy");
    WaitResult wait_res = Spin__wait(&_check_read_ready);
    if (wait_res == WAIT_OK)
    {
        res = Some(Char, (Char) (*UART0_DR & 0xFF));
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

Result(InitCode) uart_init()
{
    register unsigned int r;

    // initialize UART
    *UART0_CR = 0;           // turn off UART0

    *UART0_ICR = 0x7FF;      // clear interrupts
    *UART0_IBRD = 2;         // 115200 baud
    *UART0_FBRD = 0xB;
    *UART0_LCRH = 0b11 << 5; // 8n1
    *UART0_CR = 0x301;       // enable Tx, Rx, FIFO

    return Success(InitCode, ARCH_INIT_OK);
}

Result(InitCode) uart_enable_irqs()
{
    *UART0_CR = 0;            // disable UART
    *UART0_ICR = 0x7FF;       // clear all interrupts
    *UART0_IMSC = I_RX | I_RT; // enable RX and receive-timeout interrupts
    *UART0_CR = (1 << 9) | (1 << 8) | (1 << 0); // enable UART, TX, RX

    __d_println(IRQ2, "Vals");
    __d_printf(IRQ2, "IMSC=%x MIS=%x CR=%x\n", *UART0_IMSC, UARTMIS, *UART0_CR);

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
    uint32_t mis = UARTMIS;
    bool received_data = mis & (I_RX | I_RT);
    if (received_data)
    {
        // Drain RX FIFO
        while (!(*UART0_FR & FR_RXFE))
        {
            Char ch = (Char) *UART0_DR & 0xFF;
            __d_printf(IRQ, "Read char: %i\n", ch);
            __d_printf(IRQ, "Read char: %c\n", ch);
            IOBuffer__push(&console_state.read_buffer, ch);
        }
        // Clear RX + timeout
        UARTICR = I_RX | I_RT;
    }

    if (mis & I_TX)
    {
        // Fill TX FIFO until full or nothing left to send
        while (!(*UART0_FR & FR_TXFF))
        {
            ResultMatch(Char, IOBuffer__pop(&console_state.write_buffer))
                IsSuccess(val,
                {
                    *UART0_DR = val;
                });
            End

        }
        // Clear TX
        UARTICR = I_TX;
    }

    if (mis & I_ERR)
    {
        // Read & clear errors
        UART0_RSR_ECR = 0xFF;
        UARTICR = I_ERR;
    }
    if (received_data)
    {
        // only message io subsystem if there is a parked context
        Match(IOContextPtr, ParkedContexts__at(&console_state.parked_contexts, -1))
            IsSome(parked_context,
            {
                __d_println(IOSUB, "Parked context found");
                IOSubsystem__data_arrived(io_subsystem, parked_context);
            })
            IsNone(__d_println(IOSUB, "NO parked context found");)
        End
    }
}
