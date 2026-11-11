#ifndef __KERNEL_STACK_H
#define __KERNEL_STACK_H

#include "address.h"
#include "arch/page_table.h"

typedef struct Stack
{
    UserAddress base;
    UserAddress top;
    size_t size;
} Stack;

typedef Stack * StackPtr;
typedef const Stack * ConstStackPtr;

static inline Stack Stack__new_from_user_allocation(UserAddress base, size_t size)
{
    return (Stack){.base = base, .top = UserAddress__relative(&base, size), .size = size};
}

Stack Stack__new(size_t size);
void Stack__map(KernelPtr kernel, StackPtr self, ProcessPageTablePtr page_table);

#define NULL_STACK                                                                                                     \
    (Stack)                                                                                                            \
    {                                                                                                                  \
        .base = NULL_USER_ADDRESS, .top = NULL_USER_ADDRESS, .size = 0,                                                \
    }

#endif
