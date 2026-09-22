#include "basic_io.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../../kernel/arch/basic_io.h"

#define UART_TRANSMIT 0ULL
#define UART_LINE_STATUS 5ULL
#define UART_TRANSMIT_EMPTY (1U << 5U)

typedef volatile uint8_t * RiscVUartRegistersPtr;

static Address uart_base;

void RiscVBasicIO__configure(ConstUartInfoPtr uart)
{
    uart_base = uart->virtual_base;
}

static void RiscVBasicIO__put_char(char character)
{
    RiscVUartRegistersPtr registers = (RiscVUartRegistersPtr)uart_base;
    if (registers == (RiscVUartRegistersPtr)0)
    {
        return;
    }

    while ((registers[UART_LINE_STATUS] & UART_TRANSMIT_EMPTY) == 0)
    {
        __asm__ volatile("nop");
    }
    registers[UART_TRANSMIT] = (uint8_t)character;
}

void BasicIO__puts(const char * str)
{
    while (*str != '\0')
    {
        if (*str == '\n')
        {
            RiscVBasicIO__put_char('\r');
        }
        RiscVBasicIO__put_char(*str);
        str++;
    }
}

void BasicIO__put_int(int num)
{
    char digits[12];
    uint64_t count = 0;
    unsigned int value = num < 0 ? (unsigned int)(-(int64_t)num) : (unsigned int)num;

    if (num < 0)
    {
        RiscVBasicIO__put_char('-');
    }
    do
    {
        digits[count++] = (char)('0' + value % 10U);
        value /= 10U;
    } while (value != 0);

    while (count > 0)
    {
        RiscVBasicIO__put_char(digits[--count]);
    }
}

#ifdef KERNEL_SEMIHOSTING_TRACE
#define SEMIHOST_SYS_OPEN 0x01U
#define SEMIHOST_SYS_WRITE 0x05U
#define SEMIHOST_OPEN_WRITE 4U

static const char trace_path[] = KERNEL_TRACE_PATH;
static uintptr_t trace_handle = (uintptr_t)-1;
static bool trace_open_attempted = false;
static volatile uint32_t trace_write_lock = 0;

static uintptr_t Semihosting__call(uintptr_t operation, uintptr_t argument)
{
    register uintptr_t a0 __asm__("a0") = operation;
    register uintptr_t a1 __asm__("a1") = argument;
    __asm__ volatile(".option push\n"
                     ".option norvc\n"
                     "slli zero, zero, 0x1f\n"
                     "ebreak\n"
                     "srai zero, zero, 7\n"
                     ".option pop"
                     : "+r"(a0)
                     : "r"(a1)
                     : "memory");
    return a0;
}

static size_t DebugTrace__strlen(const char * str)
{
    size_t length = 0;
    while (str[length] != '\0')
    {
        length++;
    }
    return length;
}

static uintptr_t DebugTrace__open_handle(void)
{
    if (!trace_open_attempted)
    {
        trace_open_attempted = true;
        uintptr_t arguments[3] = {
            (uintptr_t)trace_path,
            SEMIHOST_OPEN_WRITE,
            sizeof(trace_path) - 1U,
        };
        trace_handle = Semihosting__call(SEMIHOST_SYS_OPEN, (uintptr_t)arguments);
    }
    return trace_handle;
}

static bool DebugTrace__try_lock(void)
{
    return __atomic_exchange_n(&trace_write_lock, 1U, __ATOMIC_ACQUIRE) == 0U;
}

static void DebugTrace__unlock(void)
{
    __atomic_store_n(&trace_write_lock, 0U, __ATOMIC_RELEASE);
}

void DebugTrace__puts(const char * str)
{
    if (!DebugTrace__try_lock())
    {
        return;
    }

    uintptr_t handle = DebugTrace__open_handle();
    if (handle != (uintptr_t)-1)
    {
        uintptr_t arguments[3] = {
            handle,
            (uintptr_t)str,
            DebugTrace__strlen(str),
        };
        Semihosting__call(SEMIHOST_SYS_WRITE, (uintptr_t)arguments);
    }
    DebugTrace__unlock();
}
#else
void DebugTrace__puts(const char * str)
{
    BasicIO__puts(str);
}
#endif
