#include "../../wuff/arch.h"

uint16_t WuffArch__machine(void)
{
    return 183;
}

WuffRelocationKind WuffArch__relocation_kind(uint32_t type)
{
    switch (type)
    {
    case 1027:
        return WUFF_RELOCATION_RELATIVE;
    case 257:
        return WUFF_RELOCATION_ABSOLUTE;
    case 1025:
        return WUFF_RELOCATION_GLOBAL_DATA;
    case 1026:
        return WUFF_RELOCATION_JUMP_SLOT;
    }
    return WUFF_RELOCATION_UNSUPPORTED;
}
