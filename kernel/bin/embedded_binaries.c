#include "../defs.h"
#include "elf.h"
#include "utils.h"
#include "embedded_binaries.h"

#include <stddef.h>

#include "../debug.h"

__extern_binary(_bin_hello_world);
__extern_binary(_bin_idle);
__extern_binary(_bin_shell);

static const Elf embedded_binaries[] = {
    EMBED("hello_world", _bin_hello_world),
    EMBED("idle", _bin_idle),
    EMBED("shell", _bin_shell),
};

Elf * EmbeddedElf__load(const char * name)
{
    for (int i = 0; i < sizeof(embedded_binaries) / sizeof(Elf); i++)
    {
        if (compare(embedded_binaries[i].name, name))
        {
            return &embedded_binaries[i];
        }
    }

    return NULL;
}


