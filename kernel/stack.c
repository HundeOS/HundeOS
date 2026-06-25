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
    __d_printf(
        MMU,
        "Stack__map stack=%p base=%p top=%p kbase=%p phys=%p page_table=%p\n",
        self,
        self->base.virtual_addr,
        self->top.virtual_addr,
        self->base.kernel_addr.virtual_addr,
        self->base.kernel_addr.phys_addr,
        page_table
    );
    PageTable__map(page_table, self->base, STACK_SIZE);
    __d_printf(MMU, "Stack__map done stack=%p page_table=%p\n", self, page_table);
}
