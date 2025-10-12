#ifndef __KERNEL_BIN_EMBEDDED_BINARIES_H
#define __KERNEL_BIN_EMBEDDED_BINARIES_H

#include "elf.h"

#define EMBED(bin_name, sym) \
    (Elf) { \
        .start=(Address)&sym##_start, \
        .end=(Address)&sym##_end, \
        .name=bin_name\
    }


Elf * EmbeddedElf__load(const char * name);

#endif