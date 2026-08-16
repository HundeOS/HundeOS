#ifndef __USER_SYS_UART_CONFIG_H
#define __USER_SYS_UART_CONFIG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../../common/types/bstring.h"
#include "io.h"

#define UART_CONFIG_PATH "/proc/sysinfo"
#define UART_CONFIG_SYSINFO_SIZE 16384

typedef struct UartDriverConfig
{
    uint64_t physical_base;
    uint64_t mmio_size;
    uint64_t irq;
}
UartDriverConfig;

typedef UartDriverConfig * UartDriverConfigPtr;

static uint64_t UartDriverConfig__digit_value(char ch)
{
    if (ch >= '0' && ch <= '9')
    {
        return (uint64_t) (ch - '0');
    }
    if (ch >= 'a' && ch <= 'f')
    {
        return (uint64_t) (10 + ch - 'a');
    }
    if (ch >= 'A' && ch <= 'F')
    {
        return (uint64_t) (10 + ch - 'A');
    }

    return 16;
}

static Optional(uint64_t) UartDriverConfig__parse_uint64(ConstBStringPtr text)
{
    uint64_t base = 10;
    size_t pos = 0;
    uint64_t parsed = 0;
    bool saw_digit = false;
    BString stripped = BString__strip(text);

    if (stripped.len >= 2 && stripped.data[0] == '0' && (stripped.data[1] == 'x' || stripped.data[1] == 'X'))
    {
        base = 16;
        pos = 2;
    }

    while (pos < (size_t) stripped.len)
    {
        uint64_t digit = UartDriverConfig__digit_value(stripped.data[pos]);
        if (digit >= base)
        {
            return None(uint64_t);
        }
        parsed = (parsed * base) + digit;
        saw_digit = true;
        pos++;
    }

    if (!saw_digit)
    {
        return None(uint64_t);
    }

    return Some(uint64_t, parsed);
}

static bool UartDriverConfig__read_line_value(ConstBStringPtr line, ConstBStringPtr key, uint64_t * value)
{
    BString delimiter = BSTRING_NEW("=");
    BStringSplit split = BString__split_first_any(line, &delimiter);
    if (is_nothing(split.match))
    {
        return false;
    }

    BString line_key = BString__strip(&split.match.some.val);
    if (!BString__equals(&line_key, key))
    {
        return false;
    }

    Match(uint64_t, UartDriverConfig__parse_uint64(&split.rest))
        IsSome(parsed,
        {
            *value = parsed;
            return true;
        })
    End

    return false;
}

static void UartDriverConfig__read_config_line(
    ConstBStringPtr line,
    UartDriverConfigPtr self,
    bool * found_physical_base,
    bool * found_mmio_size,
    bool * found_irq)
{
    BString physical_base_key = BSTRING_NEW("uart.physical_base");
    BString mmio_size_key = BSTRING_NEW("uart.mmio_size");
    BString irq_key = BSTRING_NEW("uart.irq");

    if (!*found_physical_base && UartDriverConfig__read_line_value(line, &physical_base_key, &self->physical_base))
    {
        *found_physical_base = true;
    }
    if (!*found_mmio_size && UartDriverConfig__read_line_value(line, &mmio_size_key, &self->mmio_size))
    {
        *found_mmio_size = true;
    }
    if (!*found_irq && UartDriverConfig__read_line_value(line, &irq_key, &self->irq))
    {
        *found_irq = true;
    }
}

static bool UartDriverConfig__read_config(char * sysinfo, ByteCount bytes, UartDriverConfigPtr self)
{
    BString line = EMPTY_BSTRING;
    bool found_physical_base = false;
    bool found_mmio_size = false;
    bool found_irq = false;

    for (ByteCount index = 0; index < bytes; index++)
    {
        if (sysinfo[index] == '\n')
        {
            UartDriverConfig__read_config_line(
                &line,
                self,
                &found_physical_base,
                &found_mmio_size,
                &found_irq);
            BString__clear(&line);
        }
        else if (!BString__push_char(&line, sysinfo[index]))
        {
            BString__clear(&line);
        }

        if (found_physical_base && found_mmio_size && found_irq)
        {
            return true;
        }
    }

    if (!BString__is_empty(&line))
    {
        UartDriverConfig__read_config_line(
            &line,
            self,
            &found_physical_base,
            &found_mmio_size,
            &found_irq);
    }

    return found_physical_base && found_mmio_size && found_irq;
}

static bool UartDriverConfig__load(UartDriverConfigPtr self)
{
    char sysinfo[UART_CONFIG_SYSINFO_SIZE];
    IOHandle handle = open(UART_CONFIG_PATH, IO_OPEN_READ);
    if (handle < 0)
    {
        return false;
    }

    ByteCount bytes = read(handle, sysinfo, sizeof(sysinfo) - 1);
    close(handle);
    if (bytes <= 0)
    {
        return false;
    }

    return UartDriverConfig__read_config(sysinfo, bytes, self);
}

#endif
