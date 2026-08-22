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

static const BString pci_config_delimiter = BSTRING_NEW("=");
static const BString pci_config_present_key = BSTRING_NEW("pci.present");
static const BString pci_config_ecam_physical_base_key = BSTRING_NEW("pci.ecam.physical_base");
static const BString pci_config_ecam_size_key = BSTRING_NEW("pci.ecam.size");
static const BString pci_config_bus_start_key = BSTRING_NEW("pci.bus.start");
static const BString pci_config_bus_end_key = BSTRING_NEW("pci.bus.end");

static Optional(uint64_t) PciConfig__read_line_value(ConstBStringPtr line, ConstBStringPtr key)
{
    BStringSplit split = BString__split_first_any(line, &pci_config_delimiter);
    bool matched = false;

    Match(BString, split.match)
        IsSome(raw_key,
        {
            BString line_key = BString__strip(&raw_key);
            matched = BString__equals(&line_key, key);
        })
    End

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
        Match(uint64_t, PciConfig__read_line_value(line, &pci_config_present_key))
            IsSome(parsed, self.found_present = parsed == 1)
        End
    }
    if (!self.found_ecam_physical_base)
    {
        Match(uint64_t, PciConfig__read_line_value(line, &pci_config_ecam_physical_base_key))
            IsSome(parsed,
            {
                self.config.ecam_physical_base = parsed;
                self.found_ecam_physical_base = true;
            })
        End
    }
    if (!self.found_ecam_size)
    {
        Match(uint64_t, PciConfig__read_line_value(line, &pci_config_ecam_size_key))
            IsSome(parsed,
            {
                self.config.ecam_size = parsed;
                self.found_ecam_size = true;
            })
        End
    }
    if (!self.found_bus_start)
    {
        Match(uint64_t, PciConfig__read_line_value(line, &pci_config_bus_start_key))
            IsSome(parsed,
            {
                self.config.bus_start = parsed;
                self.found_bus_start = true;
            })
        End
    }
    if (!self.found_bus_end)
    {
        Match(uint64_t, PciConfig__read_line_value(line, &pci_config_bus_end_key))
            IsSome(parsed,
            {
                self.config.bus_end = parsed;
                self.found_bus_end = true;
            })
        End
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

    Match(DynStringLines, DynString__lines(sysinfo))
        IsSome(lines,
        {
            LLIterate(DynStringLines, lines, line)
                Match(BString, DynString__bstring(&line))
                    IsSome(bstring_line, state = PciConfigParseState__read_config_line(state, &bstring_line))
                End
            End

            DynStringLines__dispose_all(&lines);
        })
    End

    if (!PciConfigParseState__has_required_fields(&state))
    {
        return false;
    }

    *self = state.config;

    return true;
}

static bool PciConfig__load(PciConfigPtr self)
{
    IOHandle handle = open(PCI_CONFIG_PATH, IO_OPEN_READ);
    if (handle < 0)
    {
        return false;
    }

    Optional(DynString) sysinfo = IO__read_dyn_string(handle, PCI_CONFIG_MAX);
    close(handle);

    Match(DynString, sysinfo)
        IsSome(data,
        {
            bool loaded = PciConfig__read_config(&data, self);
            DynString__dispose(&data);
            return loaded;
        })
    End

    return false;
}

#endif
