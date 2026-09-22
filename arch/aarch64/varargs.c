#include "../../kernel/arch/varargs.h"

#include <stddef.h>
#include <stdint.h>

#define K_PRINTF_AARCH64_GR_SAVE_BYTES 64U

typedef struct KPrintfAArch64VaList
{
    void * stack;
    void * gr_top;
    void * vr_top;
    int gr_offs;
    int vr_offs;
} KPrintfAArch64VaList;

typedef KPrintfAArch64VaList * KPrintfAArch64VaListPtr;

bool KernelVarArgs__has_general_arg(va_list args)
{
    KPrintfAArch64VaListPtr state = (KPrintfAArch64VaListPtr)&args;
    if (state->gr_offs < 0)
    {
        if (state->gr_top == NULL) // hundelint: ignore-no-null
        {
            return false;
        }

        uintptr_t gr_top = (uintptr_t)state->gr_top;
        uintptr_t cursor = gr_top + (uintptr_t)state->gr_offs;
        return cursor >= (gr_top - K_PRINTF_AARCH64_GR_SAVE_BYTES) && (cursor + sizeof(uint64_t)) <= gr_top &&
               (cursor & (sizeof(uint64_t) - 1U)) == 0;
    }

    return state->stack != NULL // hundelint: ignore-no-null
          && (((uintptr_t)state->stack) & (sizeof(uint64_t) - 1U)) == 0;
}
