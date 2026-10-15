#include "print.h"
#include "uart.h"
#include "mutex.h"
#include "../io/pl011.h"

#define NS16550_DATA 0U
#define NS16550_LINE_STATUS 5U
#define NS16550_TRANSMIT_EMPTY (1U << 5)

static __BOOT_DATA__(uint64_t) boot_uart_physical_base = BOOT_UART0_PHYS_BASE;
static __BOOT_DATA__(UartKind) boot_uart_kind = UART_KIND_PL011;
static __BOOT_DATA__(uint64_t) boot_uart_register_shift = 0;
static __BOOT_DATA__(uint64_t) boot_uart_register_width = 4;

static __BOOT_DATA__(boot_mutex) print_lock = { // hundelint: ignore-constructor-required
    .lock = 0};

__BOOT__(void) BootUart__configure(UartInfoPtr info)
{
    if (info->physical_base != 0)
    {
        boot_uart_physical_base = info->physical_base;
    }
    boot_uart_kind = info->kind;
    boot_uart_register_shift = info->register_shift;
    boot_uart_register_width = info->register_width;
}

static __BOOT__(uint32_t) BootUart__read(uint64_t index)
{
    uint64_t offset = boot_uart_kind == UART_KIND_NS16550 ? index << boot_uart_register_shift : index;
    Address address = boot_uart_physical_base + offset;
    return boot_uart_register_width == 1 ? *(volatile uint8_t *)address : *(volatile uint32_t *)address;
}

static __BOOT__(void) BootUart__write(uint64_t index, uint32_t value)
{
    uint64_t offset = boot_uart_kind == UART_KIND_NS16550 ? index << boot_uart_register_shift : index;
    Address address = boot_uart_physical_base + offset;
    if (boot_uart_register_width == 1)
    {
        *(volatile uint8_t *)address = (uint8_t)value;
    }
    else
    {
        *(volatile uint32_t *)address = value;
    }
}

__BOOT__(void) BootUart__init(void)
{
    if (boot_uart_kind == UART_KIND_PL011)
    {
        BootUart__write(UART0_CR_OFFSET, 0);
        BootUart__write(UART0_ICR_OFFSET, 0x7FF);
        BootUart__write(UART0_IBRD_OFFSET, 2);
        BootUart__write(UART0_FBRD_OFFSET, 0xB);
        BootUart__write(UART0_LCRH_OFFSET, 0b11 << 5);
        BootUart__write(UART0_CR_OFFSET, 0x301);
    }
}

inline void _send(unsigned int c)
{

    for (int j = 0; j < 1000; j++)
    {
        __asm__ __volatile__("nop");
        uint32_t status = boot_uart_kind == UART_KIND_NS16550 ? BootUart__read(NS16550_LINE_STATUS)
                                                              : BootUart__read(UART0_FR_OFFSET);
        bool ready = boot_uart_kind == UART_KIND_NS16550 ? (status & NS16550_TRANSMIT_EMPTY) != 0
                                                         : (status & FR_TXFF) == 0;
        if (ready)
        {
            break;
        }
    }

    BootUart__write(boot_uart_kind == UART_KIND_NS16550 ? NS16550_DATA : UART0_DR_OFFSET, c);
}

__BOOT__(void) _print(const char * str)
{
    lock(&print_lock);
    for (const char * p = str; *p; p++)
    {
        _send(*p);
        if (*p == '\n')
        {
            _send('\r'); // Add carriage return
        }
    }
    unlock(&print_lock);
}

__BOOT_STRING__(HEX_DIGITS) = "0123456789ABCDEF";

__BOOT__(void) _print_base(uint64_t value, const int base)
{
    char buffer[64]; // Enough for binary representation + null
    int i = 63;

    buffer[i] = '\0';

    if (value == 0)
    {
        buffer[--i] = '0';
    }
    else
    {
        while (value > 0 && i > 0)
        {
            buffer[--i] = HEX_DIGITS[value % base];
            value /= base;
        }
    }

    _print(&buffer[i]);
}

__BOOT__(void) _print_unsigned(uint64_t value)
{
    _print_base(value, 16);
}

__BOOT__(void) _print_binary(uint64_t value)
{
    _print_base(value, 2);
}
