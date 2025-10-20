#include "uart.h"
#include "../../../kernel/kernel.h"
#include "../../../kernel/subsystems/io/io.h"
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

// Simple string input
Result(ByteCount) read_line(BufferPtr buffer) {
    ByteCount written = 0;
    bool complete = false;
    for (Optional(Char) ch = _readchar(); complete; ch = _readchar())
    {
        Char read = 0;
        Match(Char, ch);
            IsNone(
            {
                continue;
            });
            IsSome(val,
            {
                read = val;
            })
        End
        ResultMatch(ArrayCode, Buffer__push(buffer, read));
            IsSuccess(code,
            {
                echo(read);
                if (read == '\n' || read == '\r')
                {
                    complete = true;
                }
            });
            IsError(code, message,
            {
                return Error(ByteCount, message, code);
            });
        End
        written++;
    }
    return Success(ByteCount, written);
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

void uart_init()
{
    register unsigned int r;

    // initialize UART
    *UART0_CR = 0;           // turn off UART0

    *UART0_ICR = 0x7FF;      // clear interrupts
    *UART0_IBRD = 2;         // 115200 baud
    *UART0_FBRD = 0xB;
    *UART0_LCRH = 0b11 << 5; // 8n1
    *UART0_CR = 0x301;       // enable Tx, Rx, FIFO
}
