#ifndef __USER_SYS_UART_CONFIG_H
#define __USER_SYS_UART_CONFIG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../../common/types/bstring.h"
#include "../../common/types/dyn_string.h"
#include "io.h"

#define UART_CONFIG_PATH "/boot/sysinfo"
#define UART_CONFIG_MAX 16384

typedef struct UartDriverConfig
{
    uint64_t physical_base;
    uint64_t mmio_size;
    uint64_t irq;
} UartDriverConfig;

typedef UartDriverConfig * UartDriverConfigPtr;

static const BString uart_config_delimiter = bstr("=");
static const BString uart_config_physical_base_key = bstr("uart.physical_base");
static const BString uart_config_mmio_size_key = bstr("uart.mmio_size");
static const BString uart_config_irq_key = bstr("uart.irq");

static bool UartDriverConfig__read_line_value(ConstBStringPtr line, ConstBStringPtr key, uint64_t * value)
{
    BStringSplit split = BString__split_first_any(line, &uart_config_delimiter);
    bool matched = false;

    match_optional(
        split.match,
        some(raw_key)
        {
            BString line_key = BString__strip(&raw_key);
            matched = BString__equals(&line_key, key);
        },
        none_do_nothing()
    );

    if (!matched)
    {
        return false;
    }

    match_optional(
        BString__parse_uint64_base(&split.rest, 0),
        some(parsed)
        {
            *value = parsed;
            return true;
        },
        none_do_nothing()
    );

    return false;
}

static void UartDriverConfig__read_config_line(ConstBStringPtr line, UartDriverConfigPtr self,
                                               bool * found_physical_base, bool * found_mmio_size, bool * found_irq)
{
    if (!*found_physical_base &&
        UartDriverConfig__read_line_value(line, &uart_config_physical_base_key, &self->physical_base))
    {
        *found_physical_base = true;
    }
    if (!*found_mmio_size && UartDriverConfig__read_line_value(line, &uart_config_mmio_size_key, &self->mmio_size))
    {
        *found_mmio_size = true;
    }
    if (!*found_irq && UartDriverConfig__read_line_value(line, &uart_config_irq_key, &self->irq))
    {
        *found_irq = true;
    }
}

static bool UartDriverConfig__read_config(ConstDynStringPtr sysinfo, UartDriverConfigPtr self)
{
    bool found_physical_base = false;
    bool found_mmio_size = false;
    bool found_irq = false;

    match_optional(
        DynString__lines(sysinfo),
        some(lines)
        {
            for_each_ll(
                DynStringLines,
                lines,
                line,
                {
                    match_optional(
                        DynString__bstring(&line),
                        some(bstring_line)
                        {
                            UartDriverConfig__read_config_line(&bstring_line, self, &found_physical_base,
                                                               &found_mmio_size, &found_irq);
                        },
                        none_do_nothing()
                    );
                }
            );
            DynStringLines__dispose_all(&lines);
        },
        none_do_nothing()
    );

    return found_physical_base && found_mmio_size && found_irq;
}

static bool UartDriverConfig__load(UartDriverConfigPtr self)
{
    IOHandle handle = open(&bstr(UART_CONFIG_PATH), IO_OPEN_READ);
    if (handle < 0)
    {
        return false;
    }

    Optional(DynString) sysinfo = IO__read_dyn_string(handle, UART_CONFIG_MAX);
    close(handle);

    match_optional(
        sysinfo,
        some(data)
        {
            bool loaded = UartDriverConfig__read_config(&data, self);
            DynString__dispose(&data);
            return loaded;
        },
        none_do_nothing()
    );

    return false;
}

#endif
