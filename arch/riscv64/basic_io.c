#include "basic_io.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../../kernel/arch/basic_io.h"

#define UART_TRANSMIT 0ULL
#define UART_LINE_STATUS 5ULL
#define UART_TRANSMIT_EMPTY (1U << 5U)

typedef volatile uint8_t * RiscVUartRegister8Ptr;
typedef volatile uint32_t * RiscVUartRegister32Ptr;

typedef struct RiscVUart
{
    Address base;
    uint64_t register_shift;
    uint64_t register_width;
} RiscVUart;

typedef RiscVUart * RiscVUartPtr;
typedef const RiscVUart * ConstRiscVUartPtr;

static RiscVUart console_uart;
static RiscVUart trace_uart;

static void RiscVUart__configure(RiscVUartPtr self, ConstUartInfoPtr uart)
{
    self->base = uart->virtual_base;
    self->register_shift = uart->register_shift;
    self->register_width = uart->register_width;
}

void RiscVBasicIO__configure_console(ConstUartInfoPtr uart)
{
    RiscVUart__configure(&console_uart, uart);
}

void RiscVBasicIO__configure_trace(ConstUartInfoPtr uart)
{
    RiscVUart__configure(&trace_uart, uart);
}

static uint32_t RiscVUart__read(ConstRiscVUartPtr self, uint64_t index)
{
    Address address = self->base + (index << self->register_shift);
    return self->register_width == 4 ? *(RiscVUartRegister32Ptr)address : *(RiscVUartRegister8Ptr)address;
}

static void RiscVUart__write(ConstRiscVUartPtr self, uint64_t index, uint8_t value)
{
    Address address = self->base + (index << self->register_shift);
    if (self->register_width == 4)
    {
        *(RiscVUartRegister32Ptr)address = value;
    }
    else
    {
        *(RiscVUartRegister8Ptr)address = value;
    }
}

static void RiscVUart__put_char(ConstRiscVUartPtr self, char character)
{
    if (self->base == 0)
    {
        return;
    }

    while ((RiscVUart__read(self, UART_LINE_STATUS) & UART_TRANSMIT_EMPTY) == 0)
    {
        __asm__ volatile("nop");
    }
    RiscVUart__write(self, UART_TRANSMIT, (uint8_t)character);
}

static void RiscVUart__puts(ConstRiscVUartPtr self, const char * str)
{
    while (*str != '\0')
    {
        if (*str == '\n')
        {
            RiscVUart__put_char(self, '\r');
        }
        RiscVUart__put_char(self, *str);
        str++;
    }
}

void BasicIO__puts(const char * str)
{
    RiscVUart__puts(&console_uart, str);
}

void BasicIO__put_int(int num)
{
    char digits[12];
    uint64_t count = 0;
    unsigned int value = num < 0 ? (unsigned int)(-(int64_t)num) : (unsigned int)num;

    if (num < 0)
    {
        RiscVUart__put_char(&console_uart, '-');
    }
    do
    {
        digits[count++] = (char)('0' + value % 10U);
        value /= 10U;
    } while (value != 0);

    while (count > 0)
    {
        RiscVUart__put_char(&console_uart, digits[--count]);
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
    RiscVUart__puts(&trace_uart, str);
}
#endif
