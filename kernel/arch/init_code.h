#ifndef _KERNEL_ARCH_INIT_CODE_H
#define _KERNEL_ARCH_INIT_CODE_H
#include "../types/result.h"

typedef uint64_t InitCode;
ResultDef(InitCode);

typedef enum ArchInitCode {
    ARCH_INIT_OK = 0,
    ARCH_INIT_ERROR = -1,
} ArchInitCode;

#endif