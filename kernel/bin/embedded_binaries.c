#include "../defs.h"
#include "elf.h"
#include "embedded_fs.h"
#include "embedded_binaries.h"

#include <stddef.h>

#include "../debug.h"
#include "types/kstring_additional_types.h"

extern const char __embedded_binaries_start[];
extern const char __embedded_binaries_end[];

static EmbeddedFs EmbeddedElf__filesystem()
{
    return EmbeddedFs__new((Address) __embedded_binaries_start, (Address) __embedded_binaries_end);
}

Optional(Elf) EmbeddedElf__load(KString name)
{
    EmbeddedFs filesystem = EmbeddedElf__filesystem();
    Match(EmbeddedFsFile, EmbeddedFs__find(&filesystem, name))
        IsSome(file,
        {
            return Some(Elf, Elf__new(file.start, file.end, file.name));
        })
    End

    return None(Elf);
}


KStringArray EmbeddedElf__list()
{
    EmbeddedFs filesystem = EmbeddedElf__filesystem();
    return EmbeddedFs__list(&filesystem);
}

