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

ProcessContextPtr ProcessContext__init(KernelPtr kernel, ProcessID pid, StackPtr stack, UserAddress entry_point,
                                       KernelAddress page_table, ConstProcessStartupPtr startup)
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
    context->x[0] = startup->argument_count;
    context->x[1] = startup->arguments;
    context->x[2] = startup->environment;
    context->x[3] = pid;
    context->x[7] = startup->exec_payload;
    return context;
}

void ProcessContext__copy(ProcessContextPtr self, ProcessContextPtr source)
{
    *self = *source;
}

void ProcessContext__set_syscall_result(ProcessContextPtr self, SystemCallResult result)
{
    self->x[0] = result;
}

void ProcessContext__restart_syscall(ProcessContextPtr self)
{
    self->elr -= sizeof(uint32_t);
}

ProcessContextPtr ProcessContext__clone(KernelPtr kernel, ProcessContextPtr source)
{
    ProcessContextPtr context = v_ptr(Kernel__alloc(sizeof(ProcessContext)), ProcessContext);
    ProcessContext__copy(context, source);
    return context;
}

void ProcessContext__enter_signal(ProcessContextPtr self, ProcessSignal signal, Address handler, Address restorer)
{
    self->x[0] = signal;
    self->x[30] = restorer;
    self->elr = handler;
}
