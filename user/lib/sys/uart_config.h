#ifndef __USER_SYS_UART_CONFIG_H
#define __USER_SYS_UART_CONFIG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "hardware/boot_info.h"
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
    UartKind kind;
    uint64_t register_shift;
    uint64_t register_width;
} UartDriverConfig;

typedef UartDriverConfig * UartDriverConfigPtr;

typedef struct UartDriverConfigFields
{
    bool physical_base;
    bool mmio_size;
    bool irq;
    bool kind;
    bool register_shift;
    bool register_width;
} UartDriverConfigFields;

typedef UartDriverConfigFields * UartDriverConfigFieldsPtr;

static UartDriverConfigFields UartDriverConfigFields__new(void)
{
    return (UartDriverConfigFields){0};
}

static const BString uart_config_delimiter = bstr("=");
static const BString uart_config_physical_base_key = bstr("uart.physical_base");
static const BString uart_config_mmio_size_key = bstr("uart.mmio_size");
static const BString uart_config_irq_key = bstr("uart.irq");
static const BString uart_config_kind_key = bstr("uart.kind");
static const BString uart_config_register_shift_key = bstr("uart.register_shift");
static const BString uart_config_register_width_key = bstr("uart.register_width");

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
                                               UartDriverConfigFieldsPtr fields)
{
    if (!fields->physical_base &&
        UartDriverConfig__read_line_value(line, &uart_config_physical_base_key, &self->physical_base))
    {
        fields->physical_base = true;
    }
    if (!fields->mmio_size && UartDriverConfig__read_line_value(line, &uart_config_mmio_size_key, &self->mmio_size))
    {
        fields->mmio_size = true;
    }
    if (!fields->irq && UartDriverConfig__read_line_value(line, &uart_config_irq_key, &self->irq))
    {
        fields->irq = true;
    }
    if (!fields->kind)
    {
        uint64_t kind;
        if (UartDriverConfig__read_line_value(line, &uart_config_kind_key, &kind))
        {
            self->kind = (UartKind)kind;
            fields->kind = true;
        }
    }
    if (!fields->register_shift &&
        UartDriverConfig__read_line_value(line, &uart_config_register_shift_key, &self->register_shift))
    {
        fields->register_shift = true;
    }
    if (!fields->register_width &&
        UartDriverConfig__read_line_value(line, &uart_config_register_width_key, &self->register_width))
    {
        fields->register_width = true;
    }
}

static bool UartDriverConfig__read_config(ConstDynStringPtr sysinfo, UartDriverConfigPtr self)
{
    UartDriverConfigFields fields = UartDriverConfigFields__new();

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
                            UartDriverConfig__read_config_line(&bstring_line, self, &fields);
                        },
                        none_do_nothing()
                    );
                }
            );
            DynStringLines__dispose_all(&lines);
        },
        none_do_nothing()
    );

    return fields.physical_base && fields.mmio_size && fields.irq && fields.kind && fields.register_shift &&
           fields.register_width && self->kind != UART_KIND_UNKNOWN &&
           (self->register_width == 1 || self->register_width == 4);
}

static bool UartDriverConfig__load(UartDriverConfigPtr self)
{
    match_result(
        IO__open(&bstr(UART_CONFIG_PATH), IO_OPEN_READ),
        success(handle)
        {
            match_result(
                IO__read_dyn_string(handle, UART_CONFIG_MAX),
                success(data)
                {
                    IO__close(handle);
                    bool loaded = UartDriverConfig__read_config(&data, self);
                    DynString__dispose(&data);
                    return loaded;
                },
                error(code, message)
                {
                    IO__close(handle);
                    return false;
                }
            );
            return false;
        },
        error(code, message)
        {
            return false;
        }
    );
    return false;
}

#endif
