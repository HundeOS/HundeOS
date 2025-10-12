#include "../defs.h"
#include "elf.h"
#include "utils.h"
#include "embedded_binaries.h"

#include <stddef.h>
#include <stdint.h>

#include "../debug.h"

extern const unsigned char _bin_hello_world_start[], _bin_hello_world_end[];

static const Elf embedded_binaries[] = {
    EMBED("hello_world", _bin_hello_world),
};

Elf * EmbeddedElf__load(const char * name)
{
    __d_printf(FOCUS, "Loading embedded elf: %s\n", name);
    for (int i = 0; i < sizeof(embedded_binaries) / sizeof(Elf); i++)
    {
        if (compare(embedded_binaries[i].name, name))
        {
            __d_printf(FOCUS, "Found embedded elf: %s\n", name);
            return &embedded_binaries[i];
        }
    }

    return NULL;
}


