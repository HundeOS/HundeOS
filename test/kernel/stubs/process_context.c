#include "../../../kernel/arch/process.h"
#include "../../../kernel/kalloc.h"

struct ProcessContext
{
    uint64_t x[31];
    uint64_t sp;
    uint64_t elr;
    uint64_t spsr;
    uint64_t fpcr;
    uint64_t fpsr;
};

ProcessContextPtr ProcessContext__init(KernelPtr kernel,
                                       ProcessID pid,
                                       StackPtr stack,
                                       UserAddress entry_point,
                                       KernelAddress page_table,
                                       uint64_t argc,
                                       Address argv)
{
    ProcessContextPtr context = v_ptr(Kernel__alloc(sizeof(ProcessContext)), ProcessContext);
    for (int i = 0; i < 31; i++)
    {
        context->x[i] = 0;
    }
    context->sp = vaddr(stack->top);
    context->elr = entry_point.virtual_addr;
    context->spsr = 0;
    context->fpcr = 0;
    context->fpsr = 0;
    context->x[0] = argc;
    context->x[1] = argv;
    return context;
}

void ProcessContext__dispose(ProcessContextPtr self)
{
    Kernel__free(addr(self));
}
