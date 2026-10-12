#include "stack.h"

#include "address.h"
#include "debug.h"
#include "kalloc.h"
#include "arch/page_table.h"

Stack Stack__new(size_t size)
{
    UserAddress base = Kernel__alloc_user(BASE_STACK, size, true, true, false);
    UserAddress top = UserAddress__relative(&base, size);

    return (Stack){.base = base, .top = top, .size = size};
}

void Stack__map(KernelPtr kernel, StackPtr self, ProcessPageTablePtr page_table)
{
    __d_printf(MMU, "Stack__map stack=%p base=%p top=%p kbase=%p phys=%p page_table=%p\n", self,
               self->base.virtual_addr, self->top.virtual_addr, self->base.kernel_addr.virtual_addr,
               self->base.kernel_addr.phys_addr, page_table);
    PageTable__map(page_table, kernel, self->base, self->size);
    __d_printf(MMU, "Stack__map done stack=%p page_table=%p\n", self, page_table);
}
