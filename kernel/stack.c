#include "stack.h"

#include "address.h"
#include "all_defs.h"
#include "debug.h"
#include "kalloc.h"
#include "arch/page_table.h"

Stack Stack__new()
{
    UserAddress base = Kernel__alloc_user(BASE_STACK, STACK_SIZE, true, true, false);
    UserAddress top = UserAddress__relative(&base, STACK_SIZE);

    return (Stack) {
        .base = base,
        .top = top
    };
}

void Stack__map(Stack * self, PageTable * page_table)
{
    __d_printf(DEBUG, "Mapping stack %p\n", self->base);
    __d_printf(DEBUG, "Pagetable %p\n", page_table);
    PageTable__map(page_table, self->base, STACK_SIZE);
}
