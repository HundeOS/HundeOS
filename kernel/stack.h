#ifndef __KERNEL_STACK_H
#define __KERNEL_STACK_H

#include "address.h"
#include "arch/page_table.h"

typedef struct Stack
{
    UserAddress base;
    UserAddress top;
} Stack;

typedef Stack * StackPtr;

Stack Stack__new();
void Stack__map(KernelPtr kernel, Stack * self, ProcessPageTablePtr page_table);

#define NULL_STACK (Stack) { \
    .base = NULL_USER_ADDRESS, \
    .top = NULL_USER_ADDRESS, \
} \

#endif
