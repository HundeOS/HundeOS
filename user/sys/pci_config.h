#ifndef __USER_SYS_PCI_CONFIG_H
#define __USER_SYS_PCI_CONFIG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../../common/types/bstring.h"
#include "../../common/types/dyn_string.h"
#include "io.h"

#define PCI_CONFIG_PATH "/proc/sysinfo"
#define PCI_CONFIG_MAX 32768

typedef struct PciConfig
{
    uint64_t ecam_physical_base;
    uint64_t ecam_size;
    uint64_t bus_start;
    uint64_t bus_end;
}
PciConfig;

typedef PciConfig * PciConfigPtr;

typedef struct PciConfigParseState
{
    PciConfig config;
    bool found_present;
    bool found_ecam_physical_base;
    bool found_ecam_size;
    bool found_bus_start;
    bool found_bus_end;
}
PciConfigParseState;

typedef PciConfigParseState * PciConfigParseStatePtr;
typedef const PciConfigParseState * ConstPciConfigParseStatePtr;

static const BString pci_config_delimiter = bstr("=");
static const BString pci_config_present_key = bstr("pci.present");
static const BString pci_config_ecam_physical_base_key = bstr("pci.ecam.physical_base");
static const BString pci_config_ecam_size_key = bstr("pci.ecam.size");
static const BString pci_config_bus_start_key = bstr("pci.bus.start");
static const BString pci_config_bus_end_key = bstr("pci.bus.end");

static Optional(uint64_t) PciConfig__read_line_value(ConstBStringPtr line, ConstBStringPtr key)
{
    BStringSplit split = BString__split_first_any(line, &pci_config_delimiter);
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

static PciConfigParseState PciConfigParseState__new(void)
{
    PciConfigParseState self = { 0 };

    return self;
}

static PciConfigParseState PciConfigParseState__read_config_line(PciConfigParseState self, ConstBStringPtr line)
{
    if (!self.found_present)
    {
        match_optional(uint64_t, PciConfig__read_line_value(line, &pci_config_present_key),
            some(parsed) { self.found_present = parsed == 1; },
            none_do_nothing()
        );
    }
    if (!self.found_ecam_physical_base)
    {
        match_optional(uint64_t, PciConfig__read_line_value(line, &pci_config_ecam_physical_base_key),
            some(parsed)
            {
                self.config.ecam_physical_base = parsed;
                self.found_ecam_physical_base = true;
            },
            none_do_nothing()
        );
    }
    if (!self.found_ecam_size)
    {
        match_optional(uint64_t, PciConfig__read_line_value(line, &pci_config_ecam_size_key),
            some(parsed)
            {
                self.config.ecam_size = parsed;
                self.found_ecam_size = true;
            },
            none_do_nothing()
        );
    }
    if (!self.found_bus_start)
    {
        match_optional(uint64_t, PciConfig__read_line_value(line, &pci_config_bus_start_key),
            some(parsed)
            {
                self.config.bus_start = parsed;
                self.found_bus_start = true;
            },
            none_do_nothing()
        );
    }
    if (!self.found_bus_end)
    {
        match_optional(uint64_t, PciConfig__read_line_value(line, &pci_config_bus_end_key),
            some(parsed)
            {
                self.config.bus_end = parsed;
                self.found_bus_end = true;
            },
            none_do_nothing()
        );
    }

    return self;
}

static bool PciConfigParseState__has_required_fields(ConstPciConfigParseStatePtr self)
{
    return self->found_present
        && self->found_ecam_physical_base
        && self->found_ecam_size
        && self->found_bus_start
        && self->found_bus_end;
}

static bool PciConfig__read_config(ConstDynStringPtr sysinfo, PciConfigPtr self)
{
    PciConfigParseState state = PciConfigParseState__new();

    match_optional(DynStringLines, DynString__lines(sysinfo),
        some(lines)
        {
            for_each_ll(DynStringLines, lines, line,
            {
                match_optional(BString, DynString__bstring(&line),
                    some(bstring_line) { state = PciConfigParseState__read_config_line(state, &bstring_line); },
                    none_do_nothing()
                );
            });
            DynStringLines__dispose_all(&lines);
        },
        none_do_nothing()
    );

    if (!PciConfigParseState__has_required_fields(&state))
    {
        return false;
    }

    *self = state.config;

    return true;
}

static bool PciConfig__load(PciConfigPtr self)
{
    BString config_path = bstr(PCI_CONFIG_PATH);
    IOHandle handle = open(&config_path, IO_OPEN_READ);
    if (handle < 0)
    {
        return false;
    }

    Optional(DynString) sysinfo = IO__read_dyn_string(handle, PCI_CONFIG_MAX);
    close(handle);

    match_optional(DynString, sysinfo,
        some(data)
        {
            bool loaded = PciConfig__read_config(&data, self);
            DynString__dispose(&data);
            return loaded;
        },
        none_do_nothing()
    );

    return false;
}

#endif
