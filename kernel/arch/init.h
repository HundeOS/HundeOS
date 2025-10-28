#ifndef _KERNEL_ARCH_INIT_H
#define _KERNEL_ARCH_INIT_H
#include <stdint.h>

#include "../types/result.h"

typedef uint64_t InitCode;
ResultDef(InitCode);

extern Result(InitCode) Arch__init();

typedef enum ArchInitCode {
    ARCH_INIT_OK = 0,
    ARCH_INIT_ERROR = -1,
} ArchInitCode;

#endif