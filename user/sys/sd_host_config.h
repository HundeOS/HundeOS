#ifndef __USER_SYS_SD_HOST_CONFIG_H
#define __USER_SYS_SD_HOST_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

#include "../../common/types/bstring.h"
#include "../../common/types/dyn_string.h"
#include "io.h"

#define SD_HOST_CONFIG_PATH "/proc/sysinfo"
#define SD_HOST_CONFIG_MAX 32768

typedef struct SdHostConfig
{
    uint64_t physical_base;
    uint64_t mmio_size;
    uint64_t irq;
}
SdHostConfig;

typedef SdHostConfig * SdHostConfigPtr;
typedef bool * BoolPtr;
typedef uint64_t * UInt64Ptr;

OptionalDef(SdHostConfig);

static const BString sd_host_config_delimiter = BSTRING_NEW("=");
static const BString sd_host_config_present_key = BSTRING_NEW("sd_host.present");
static const BString sd_host_config_physical_base_key = BSTRING_NEW("sd_host.physical_base");
static const BString sd_host_config_mmio_size_key = BSTRING_NEW("sd_host.mmio_size");
static const BString sd_host_config_irq_key = BSTRING_NEW("sd_host.irq");

static bool SdHostConfig__read_line_value(ConstBStringPtr line, ConstBStringPtr key, UInt64Ptr value)
{
    BStringSplit split = BString__split_first_any(line, &sd_host_config_delimiter);
    bool matched = false;

    match_optional(BString, split.match,
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

    match_optional(uint64_t, BString__parse_uint64_base(&split.rest, 0),
        some(parsed)
        {
            *value = parsed;
            return true;
        },
        none_do_nothing()
    );

    return false;
}

static void SdHostConfig__read_config_line(
    ConstBStringPtr line,
    SdHostConfigPtr self,
    BoolPtr found_present,
    BoolPtr found_physical_base,
    BoolPtr found_mmio_size,
    BoolPtr found_irq)
{
    uint64_t parsed = 0;

    if (!*found_present && SdHostConfig__read_line_value(line, &sd_host_config_present_key, &parsed))
    {
        *found_present = parsed == 1;
    }
    if (!*found_physical_base && SdHostConfig__read_line_value(line, &sd_host_config_physical_base_key, &self->physical_base))
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
}

static Optional(SdHostConfig) SdHostConfig__parse(ConstDynStringPtr sysinfo)
{
    SdHostConfig config = { 0 };
    bool found_present = false;
    bool found_physical_base = false;
    bool found_mmio_size = false;
    bool found_irq = false;

    match_optional(DynStringLines, DynString__lines(sysinfo),
        some(lines)
        {
            for_each_ll(DynStringLines, lines, line,
            {
                match_optional(BString, DynString__bstring(&line),
                    some(bstring_line)
                    {
                        SdHostConfig__read_config_line(
                        &bstring_line,
                        &config,
                        &found_present,
                        &found_physical_base,
                        &found_mmio_size,
                        &found_irq
                    ); },
                    none_do_nothing()
                );
            });
            DynStringLines__dispose_all(&lines);
        },
        none_do_nothing()
    );

    if (found_present && found_physical_base && found_mmio_size && found_irq)
    {
        return Some(SdHostConfig, config);
    }

    return None(SdHostConfig);
}

static Optional(SdHostConfig) SdHostConfig__load(void)
{
    IOHandle handle = open(SD_HOST_CONFIG_PATH, IO_OPEN_READ);
    if (handle < 0)
    {
        return None(SdHostConfig);
    }

    Optional(DynString) sysinfo = IO__read_dyn_string(handle, SD_HOST_CONFIG_MAX);
    close(handle);

    match_optional(DynString, sysinfo,
        some(data)
        {
            Optional(SdHostConfig) config = SdHostConfig__parse(&data);
            DynString__dispose(&data);
            return config;
        },
        none_do_nothing()
    );

    return None(SdHostConfig);
}

#endif
