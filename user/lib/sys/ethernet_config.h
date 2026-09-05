#ifndef __USER_SYS_ETHERNET_CONFIG_H
#define __USER_SYS_ETHERNET_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

#include "../../common/types/bstring.h"
#include "../../common/types/dyn_string.h"
#include "hardware/ethernet.h"
#include "io.h"

#define ETHERNET_CONFIG_PATH "/proc/sysinfo"
#define ETHERNET_CONFIG_MAX 32768

typedef struct EthernetConfig
{
    EthernetMacKind mac_kind;
    uint64_t physical_base;
    uint64_t mmio_size;
    uint64_t irq;
}
EthernetConfig;

typedef EthernetConfig * EthernetConfigPtr;

OptionalDef(EthernetConfig);

typedef struct EthernetConfigParseState
{
    EthernetConfig config;
    bool found_present;
    bool found_mac_kind;
    bool found_physical_base;
    bool found_mmio_size;
    bool found_irq;
}
EthernetConfigParseState;

typedef EthernetConfigParseState * EthernetConfigParseStatePtr;
typedef const EthernetConfigParseState * ConstEthernetConfigParseStatePtr;

static EthernetConfig EthernetConfig__new(
    EthernetMacKind mac_kind,
    uint64_t physical_base,
    uint64_t mmio_size,
    uint64_t irq)
{
    return (EthernetConfig) {
        .mac_kind = mac_kind,
        .physical_base = physical_base,
        .mmio_size = mmio_size,
        .irq = irq,
    };
}

static EthernetConfigParseState EthernetConfigParseState__new(void)
{
    return (EthernetConfigParseState) {
        .config = EthernetConfig__new(ETHERNET_MAC_KIND_NONE, 0, 0, 0),
        .found_present = false,
        .found_mac_kind = false,
        .found_physical_base = false,
        .found_mmio_size = false,
        .found_irq = false,
    };
}

static const BString ethernet_config_delimiter = bstr("=");
static const BString ethernet_config_present_key = bstr("ethernet.present");
static const BString ethernet_config_mac_kind_key = bstr("ethernet.mac_kind");
static const BString ethernet_config_physical_base_key = bstr("ethernet.physical_base");
static const BString ethernet_config_mmio_size_key = bstr("ethernet.mmio_size");
static const BString ethernet_config_irq_key = bstr("ethernet.irq");

static Optional(uint64_t) EthernetConfig__read_line_value(ConstBStringPtr line, ConstBStringPtr key)
{
    BStringSplit split = BString__split_first_any(line, &ethernet_config_delimiter);
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
        return None(uint64_t);
    }

    return BString__parse_uint64_base(&split.rest, 0);
}

static void EthernetConfigParseState__read_config_line(
    EthernetConfigParseStatePtr self,
    ConstBStringPtr line)
{
    if (!self->found_present)
    {
        match_optional(uint64_t, EthernetConfig__read_line_value(line, &ethernet_config_present_key),
            some(parsed) { self->found_present = parsed == 1; },
            none_do_nothing()
        );
    }

    if (!self->found_mac_kind)
    {
        match_optional(uint64_t, EthernetConfig__read_line_value(line, &ethernet_config_mac_kind_key),
            some(parsed)
            {
                self->config.mac_kind = (EthernetMacKind) parsed;
                self->found_mac_kind = true;
            },
            none_do_nothing()
        );
    }

    if (!self->found_physical_base)
    {
        match_optional(uint64_t, EthernetConfig__read_line_value(line, &ethernet_config_physical_base_key),
            some(parsed)
            {
                self->config.physical_base = parsed;
                self->found_physical_base = true;
            },
            none_do_nothing()
        );
    }

    if (!self->found_mmio_size)
    {
        match_optional(uint64_t, EthernetConfig__read_line_value(line, &ethernet_config_mmio_size_key),
            some(parsed)
            {
                self->config.mmio_size = parsed;
                self->found_mmio_size = true;
            },
            none_do_nothing()
        );
    }

    if (!self->found_irq)
    {
        match_optional(uint64_t, EthernetConfig__read_line_value(line, &ethernet_config_irq_key),
            some(parsed)
            {
                self->config.irq = parsed;
                self->found_irq = true;
            },
            none_do_nothing()
        );
    }
}

static bool EthernetConfigParseState__has_required_fields(ConstEthernetConfigParseStatePtr self)
{
    return self->found_present
        && self->found_mac_kind
        && self->found_physical_base
        && self->found_mmio_size
        && self->found_irq;
}

static Optional(EthernetConfig) EthernetConfig__parse(ConstDynStringPtr sysinfo)
{
    EthernetConfigParseState state = EthernetConfigParseState__new();

    match_optional(DynStringLines, DynString__lines(sysinfo),
        some(lines)
        {
            for_each_ll(DynStringLines, lines, line,
            {
                match_optional(BString, DynString__bstring(&line),
                    some(bstring_line) { EthernetConfigParseState__read_config_line(&state, &bstring_line); },
                    none_do_nothing()
                );
            });
            DynStringLines__dispose_all(&lines);
        },
        none_do_nothing()
    );

    if (EthernetConfigParseState__has_required_fields(&state))
    {
        return Some(EthernetConfig, state.config);
    }

    return None(EthernetConfig);
}

static Optional(EthernetConfig) EthernetConfig__load(void)
{
    IOHandle handle = open(&bstr(ETHERNET_CONFIG_PATH), IO_OPEN_READ);
    if (handle < 0)
    {
        return None(EthernetConfig);
    }

    Optional(DynString) sysinfo = IO__read_dyn_string(handle, ETHERNET_CONFIG_MAX);
    close(handle);

    match_optional(DynString, sysinfo,
        some(data)
        {
            Optional(EthernetConfig) config = EthernetConfig__parse(&data);
            DynString__dispose(&data);
            return config;
        },
        none_do_nothing()
    );

    return None(EthernetConfig);
}

#endif
