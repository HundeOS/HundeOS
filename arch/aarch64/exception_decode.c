#include "exception_decode.h"

const char * AArch64Exception__class_name(uint64_t ec)
{
    switch (ec)
    {
    case 0x00:
        return "unknown reason";
    case 0x15:
        return "SVC from AArch64";
    case 0x18:
        return "trapped MSR, MRS or system instruction";
    case 0x20:
        return "instruction abort from lower EL";
    case 0x21:
        return "instruction abort from current EL";
    case 0x22:
        return "PC alignment fault";
    case 0x24:
        return "data abort from lower EL";
    case 0x25:
        return "data abort from current EL";
    case 0x26:
        return "SP alignment fault";
    case 0x2f:
        return "SError interrupt";
    case 0x3c:
        return "BRK from AArch64";
    default:
        return "unhandled exception class";
    }
}

uint64_t AArch64Exception__fault_status_code(uint64_t esr)
{
    return esr & 0x3f;
}

bool AArch64Exception__has_fault_status(uint64_t ec)
{
    return ec == 0x20 || ec == 0x21 || ec == 0x24 || ec == 0x25;
}

const char * AArch64Exception__fault_status_name(uint64_t esr)
{
    switch (AArch64Exception__fault_status_code(esr))
    {
    case 0x00:
        return "address size fault, level 0";
    case 0x01:
        return "address size fault, level 1";
    case 0x02:
        return "address size fault, level 2";
    case 0x03:
        return "address size fault, level 3";
    case 0x04:
        return "translation fault, level 0";
    case 0x05:
        return "translation fault, level 1";
    case 0x06:
        return "translation fault, level 2";
    case 0x07:
        return "translation fault, level 3";
    case 0x09:
        return "access flag fault, level 1";
    case 0x0a:
        return "access flag fault, level 2";
    case 0x0b:
        return "access flag fault, level 3";
    case 0x0d:
        return "permission fault, level 1";
    case 0x0e:
        return "permission fault, level 2";
    case 0x0f:
        return "permission fault, level 3";
    case 0x10:
        return "synchronous external abort";
    case 0x11:
        return "synchronous tag check fault";
    case 0x18:
        return "synchronous parity or ECC error";
    case 0x21:
        return "alignment fault";
    case 0x30:
        return "TLB conflict abort";
    case 0x34:
        return "unsupported atomic hardware update fault";
    default:
        return "unknown fault status";
    }
}

const char * AArch64Exception__data_abort_access_name(uint64_t esr)
{
    return (esr & (1ULL << 6)) ? "write" : "read";
}
