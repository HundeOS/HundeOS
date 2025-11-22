#include "../defs.h"
#include "elf.h"
#include "utils.h"
#include "embedded_binaries.h"

#include <stddef.h>

#include "../debug.h"
#include "types/kstring_additional_types.h"

__extern_binary(_bin_hello_world);
__extern_binary(_bin_idle);
__extern_binary(_bin_shell);

static const Elf embedded_binaries[] = {
    EMBED("hello_world", _bin_hello_world),
    EMBED("idle", _bin_idle),
    EMBED("shell", _bin_shell),
};

Optional(Elf) EmbeddedElf__load(KString name)
{
    for (int i = 0; i < sizeof(embedded_binaries) / sizeof(Elf); i++)
    {
        if (KString__equals(&embedded_binaries[i].name, &name))
        {
            return Some(Elf, embedded_binaries[i]);
        }
    }

    return None(Elf);
}


KStringArray EmbeddedElf__list()
{
    KStringArray result = KStringArray__new();
    for (int i = 0; i < sizeof(embedded_binaries) / sizeof(Elf); i++)
    {
        KStringArray__push(&result, embedded_binaries[i].name);
    }

    return result;
}


