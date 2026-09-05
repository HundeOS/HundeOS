#ifndef __KERNEL_BIN_EMBEDDED_BINARIES_H
#define __KERNEL_BIN_EMBEDDED_BINARIES_H

#include "elf.h"

Optional(Elf) EmbeddedElf__load(ConstBStringPtr name);
BStringArray EmbeddedElf__list();

#endif
