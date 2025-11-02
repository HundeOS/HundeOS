#include "../../../kernel/arch/process.h"

ProcessContextPtr ProcessContext__init(StackPtr stack, UserAddress entry_point, KernelAddress page_table)
{
    return k_v_ptr(stack->top, ProcessContext);
}