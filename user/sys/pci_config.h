#ifndef __USER_SYS_PCI_CONFIG_H
#define __USER_SYS_PCI_CONFIG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../../common/types/bstring.h"
#include "io.h"

#define PCI_CONFIG_PATH "/proc/sysinfo"
#define PCI_CONFIG_SYSINFO_SIZE 32768

typedef struct PciConfig
{
    uint64_t ecam_physical_base;
    uint64_t ecam_size;
    uint64_t bus_start;
    uint64_t bus_end;
}
PciConfig;

typedef PciConfig * PciConfigPtr;

static uint64_t PciConfig__digit_value(char ch)
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

static Optional(uint64_t) PciConfig__parse_uint64(ConstBStringPtr text)
{
    uint64_t base = 10;
    uint64_t parsed = 0;
    bool saw_digit = false;
    BString stripped = BString__strip(text);

    Match(char, BString__char_at(&stripped, 0))
        IsSome(first,
        {
            Match(char, BString__char_at(&stripped, 1))
                IsSome(second,
                {
                    if (first == '0' && (second == 'x' || second == 'X'))
                    {
                        base = 16;
                        stripped = BString__slice(&stripped, 2, -1);
                    }
                })
            End
        })
    End

    BStringIterate(&stripped, index, ch)
        uint64_t digit = PciConfig__digit_value(ch);
        if (digit >= base)
        {
            return None(uint64_t);
        }
        parsed = (parsed * base) + digit;
        saw_digit = true;
    }

    if (!saw_digit)
    {
        return None(uint64_t);
    }

    return Some(uint64_t, parsed);
}

static bool PciConfig__read_line_value(ConstBStringPtr line, ConstBStringPtr key, uint64_t * value)
{
    BString delimiter = BSTRING_NEW("=");
    BStringSplit split = BString__split_first_any(line, &delimiter);
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
        return false;
    }

    Match(uint64_t, PciConfig__parse_uint64(&split.rest))
        IsSome(parsed,
        {
            *value = parsed;
            return true;
        })
    End

    return false;
}

static void PciConfig__read_config_line(
    ConstBStringPtr line,
    PciConfigPtr self,
    bool * found_present,
    bool * found_ecam_physical_base,
    bool * found_ecam_size,
    bool * found_bus_start,
    bool * found_bus_end)
{
    BString present_key = BSTRING_NEW("pci.present");
    BString ecam_physical_base_key = BSTRING_NEW("pci.ecam.physical_base");
    BString ecam_size_key = BSTRING_NEW("pci.ecam.size");
    BString bus_start_key = BSTRING_NEW("pci.bus.start");
    BString bus_end_key = BSTRING_NEW("pci.bus.end");
    uint64_t parsed = 0;

    if (!*found_present && PciConfig__read_line_value(line, &present_key, &parsed))
    {
        *found_present = parsed == 1;
    }
    if (!*found_ecam_physical_base && PciConfig__read_line_value(line, &ecam_physical_base_key, &self->ecam_physical_base))
    {
        *found_ecam_physical_base = true;
    }
    if (!*found_ecam_size && PciConfig__read_line_value(line, &ecam_size_key, &self->ecam_size))
    {
        *found_ecam_size = true;
    }
    if (!*found_bus_start && PciConfig__read_line_value(line, &bus_start_key, &self->bus_start))
    {
        *found_bus_start = true;
    }
    if (!*found_bus_end && PciConfig__read_line_value(line, &bus_end_key, &self->bus_end))
    {
        *found_bus_end = true;
    }
}

static bool PciConfig__has_required_fields(
    bool found_present,
    bool found_ecam_physical_base,
    bool found_ecam_size,
    bool found_bus_start,
    bool found_bus_end)
{
    return found_present
        && found_ecam_physical_base
        && found_ecam_size
        && found_bus_start
        && found_bus_end;
}

static bool PciConfig__read_config(char * sysinfo, ByteCount bytes, PciConfigPtr self)
{
    BString line = EMPTY_BSTRING;
    bool found_present = false;
    bool found_ecam_physical_base = false;
    bool found_ecam_size = false;
    bool found_bus_start = false;
    bool found_bus_end = false;

    for (ByteCount index = 0; index < bytes; index++)
    {
        if (sysinfo[index] == '\n')
        {
            PciConfig__read_config_line(
                &line,
                self,
                &found_present,
                &found_ecam_physical_base,
                &found_ecam_size,
                &found_bus_start,
                &found_bus_end);
            BString__clear(&line);
        }
        else if (!BString__push_char(&line, sysinfo[index]))
        {
            BString__clear(&line);
        }

        if (PciConfig__has_required_fields(
            found_present,
            found_ecam_physical_base,
            found_ecam_size,
            found_bus_start,
            found_bus_end))
        {
            return true;
        }
    }

    if (!BString__is_empty(&line))
    {
        PciConfig__read_config_line(
            &line,
            self,
            &found_present,
            &found_ecam_physical_base,
            &found_ecam_size,
            &found_bus_start,
            &found_bus_end);
    }

    return PciConfig__has_required_fields(
        found_present,
        found_ecam_physical_base,
        found_ecam_size,
        found_bus_start,
        found_bus_end);
}

static bool PciConfig__load(PciConfigPtr self)
{
    char sysinfo[PCI_CONFIG_SYSINFO_SIZE];
    IOHandle handle = open(PCI_CONFIG_PATH, IO_OPEN_READ);
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

    return PciConfig__read_config(sysinfo, bytes, self);
}

#endif
