#include "../../wuff/arch.h"

uint16_t WuffArch__machine(void)
{
    return 243;
}

WuffRelocationKind WuffArch__relocation_kind(uint32_t type)
{
    switch (type)
    {
    case 3:
        return WUFF_RELOCATION_RELATIVE;
    case 2:
        return WUFF_RELOCATION_ABSOLUTE;
    case 5:
        return WUFF_RELOCATION_JUMP_SLOT;
    }
    return WUFF_RELOCATION_UNSUPPORTED;
}
