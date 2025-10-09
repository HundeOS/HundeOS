#ifndef __KERNEL_BIN_EMBEDDED_BINARIES_H
#define __KERNEL_BIN_EMBEDDED_BINARIES_H
#include <stddef.h>

#include "elf.h"

typedef struct EmbeddedElf {
    const void *start;
    const void *end;
    const char *name;
} EmbeddedElf;


#define EMBED(bin_name, sym) \
    (EmbeddedElf) { \
        .start=&sym##_start, \
        .end=&sym##_end, \
        .name=bin_name\
    }

Elf64Header * EmbeddedElf__read_header(const EmbeddedElf * self);
EmbeddedElf * EmbeddedElf__load(const char * name);

#endif