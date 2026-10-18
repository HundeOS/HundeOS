#ifndef __USER_SYS_SD_HOST_CONFIG_H
#define __USER_SYS_SD_HOST_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

#include "../../common/types/bstring.h"
#include "../../common/types/dyn_string.h"
#include "../../../common/hardware/boot_info.h"
#include "io.h"

#define SD_HOST_CONFIG_PATH "/boot/sysinfo"
#define SD_HOST_CONFIG_MAX 32768

typedef struct SdHostConfig
{
    uint64_t physical_base;
    uint64_t mmio_size;
    uint64_t irq;
    SdHostKind kind;
    uint64_t source_clock_hz;
} SdHostConfig;

typedef SdHostConfig * SdHostConfigPtr;
typedef bool * BoolPtr;
typedef uint64_t * UInt64Ptr;

OptionalDef(SdHostConfig);

static SdHostConfig SdHostConfig__new(uint64_t physical_base, uint64_t mmio_size, uint64_t irq, SdHostKind kind,
                                      uint64_t source_clock_hz)
{
    return (SdHostConfig){
        .physical_base = physical_base,
        .mmio_size = mmio_size,
        .irq = irq,
        .kind = kind,
        .source_clock_hz = source_clock_hz,
    };
}

static const BString sd_host_config_delimiter = bstr("=");
static const BString sd_host_config_present_key = bstr("sd_host.present");
static const BString sd_host_config_physical_base_key = bstr("sd_host.physical_base");
static const BString sd_host_config_mmio_size_key = bstr("sd_host.mmio_size");
static const BString sd_host_config_irq_key = bstr("sd_host.irq");
static const BString sd_host_config_kind_key = bstr("sd_host.kind");
static const BString sd_host_config_source_clock_key = bstr("sd_host.source_clock_hz");

static bool SdHostConfig__read_line_value(ConstBStringPtr line, ConstBStringPtr key, UInt64Ptr value)
{
    BStringSplit split = BString__split_first_any(line, &sd_host_config_delimiter);
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

static void SdHostConfig__read_config_line(ConstBStringPtr line, SdHostConfigPtr self, BoolPtr found_present,
                                           BoolPtr found_physical_base, BoolPtr found_mmio_size, BoolPtr found_irq,
                                           BoolPtr found_kind, BoolPtr found_source_clock)
{
    uint64_t parsed = 0;

    if (!*found_present && SdHostConfig__read_line_value(line, &sd_host_config_present_key, &parsed))
    {
        *found_present = parsed == 1;
    }
    if (!*found_physical_base &&
        SdHostConfig__read_line_value(line, &sd_host_config_physical_base_key, &self->physical_base))
    {
        *found_physical_base = true;
    }
    if (!*found_mmio_size && SdHostConfig__read_line_value(line, &sd_host_config_mmio_size_key, &self->mmio_size))
    {
        *found_mmio_size = true;
    }
    if (!*found_irq && SdHostConfig__read_line_value(line, &sd_host_config_irq_key, &self->irq))
    {
        *found_irq = true;
    }
    if (!*found_kind && SdHostConfig__read_line_value(line, &sd_host_config_kind_key, &parsed))
    {
        self->kind = (SdHostKind)parsed;
        *found_kind = true;
    }
    if (!*found_source_clock &&
        SdHostConfig__read_line_value(line, &sd_host_config_source_clock_key, &self->source_clock_hz))
    {
        *found_source_clock = true;
    }
}

static Optional(SdHostConfig) SdHostConfig__parse(ConstDynStringPtr sysinfo)
{
    SdHostConfig config = SdHostConfig__new(0, 0, 0, SD_HOST_KIND_GENERIC, 0);
    bool found_present = false;
    bool found_physical_base = false;
    bool found_mmio_size = false;
    bool found_irq = false;
    bool found_kind = false;
    bool found_source_clock = false;

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
                            SdHostConfig__read_config_line(&bstring_line, &config, &found_present,
                                                           &found_physical_base, &found_mmio_size, &found_irq,
                                                           &found_kind, &found_source_clock);
                        },
                        none_do_nothing()
                    );
                }
            );
            DynStringLines__dispose_all(&lines);
        },
        none_do_nothing()
    );

    if (found_present && found_physical_base && found_mmio_size && found_irq && found_kind && found_source_clock)
    {
        return Some(SdHostConfig, config);
    }

    return None(SdHostConfig);
}

static Optional(SdHostConfig) SdHostConfig__load(void)
{
    match_result(
        IO__open(&bstr(SD_HOST_CONFIG_PATH), IO_OPEN_READ),
        success(handle)
        {
            match_result(
                IO__read_dyn_string(handle, SD_HOST_CONFIG_MAX),
                success(data)
                {
                    IO__close(handle);
                    Optional(SdHostConfig) config = SdHostConfig__parse(&data);
                    DynString__dispose(&data);
                    return config;
                },
                error(code, message)
                {
                    IO__close(handle);
                    return None(SdHostConfig);
                }
            );
            return None(SdHostConfig);
        },
        error(code, message)
        {
            return None(SdHostConfig);
        }
    );
    return None(SdHostConfig);
}

#endif
