#ifndef __USER_LIB_WUFF_ARCH_H
#define __USER_LIB_WUFF_ARCH_H

#include <stdint.h>

typedef enum WuffRelocationKind
{
    WUFF_RELOCATION_UNSUPPORTED,
    WUFF_RELOCATION_RELATIVE,
    WUFF_RELOCATION_ABSOLUTE,
    WUFF_RELOCATION_GLOBAL_DATA,
    WUFF_RELOCATION_JUMP_SLOT,
} WuffRelocationKind;

uint16_t WuffArch__machine(void);
WuffRelocationKind WuffArch__relocation_kind(uint32_t type);

#endif
