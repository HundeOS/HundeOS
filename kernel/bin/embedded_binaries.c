#include "../defs.h"
#include "elf.h"
#include "utils.h"
#include "embedded_binaries.h"
#include "../page_alloc.h"

extern const unsigned char _bin_hello_world_start[], _bin_hello_world_end[];

static const EmbeddedElf embedded_binaries[] = {
    EMBED("hello_world", _bin_hello_world),
};


Elf64Header * EmbeddedElf__read_header(const EmbeddedElf * self)
{
    return (Elf64Header *) self->start;
}

EmbeddedElf * EmbeddedElf__load(const char * name)
{
    for (int i = 0; i < sizeof(embedded_binaries) / sizeof(EmbeddedElf); i++)
    {
        if (compare(embedded_binaries[i].name, name))
        {
            return &embedded_binaries[i];
        }
    }

    return NULL;
}

KernelAddress Elf__map(EmbeddedElf * self) {
    Elf64Header * header = (Elf64Header *) self->start;

    return (KernelAddress){
        .phys_addr = 0,
        .virtual_addr = 0,
        .valid = false
    };
}

