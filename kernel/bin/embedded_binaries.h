#ifndef __KERNEL_BIN_EMBEDDED_BINARIES_H
#define __KERNEL_BIN_EMBEDDED_BINARIES_H

#include "elf.h"

#define __extern_binary(name) \
    extern const char name##_start[]; \
    extern const char name##_end[];

#define EMBED(bin_name, sym) \
    (Elf) { \
        .start=(Address)&sym##_start, \
        .end=(Address)&sym##_end, \
        .name=KSTRING_NEW(bin_name)\
    }

Optional(Elf) EmbeddedElf__load(KString name);
KStringArray EmbeddedElf__list();

#endif