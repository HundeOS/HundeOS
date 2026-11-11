#include "../../../kernel/arch/process.h"
#include "../../../kernel/kalloc.h"

struct ThreadContext
{
    uint64_t x[31];
    uint64_t sp;
    uint64_t elr;
    uint64_t spsr;
    uint64_t fpcr;
    uint64_t fpsr;
    uint64_t thread_pointer;
};

ThreadContextPtr ThreadContext__init(KernelPtr kernel, ProcessID pid, StackPtr stack, UserAddress entry_point,
                                     KernelAddress page_table, ConstProcessStartupPtr startup)
{
    ThreadContextPtr context = v_ptr(Kernel__alloc(sizeof(ThreadContext)), ThreadContext);
    for (int i = 0; i < 31; i++)
    {
        context->x[i] = 0;
    }
    context->sp = vaddr(stack->top);
    context->elr = entry_point.virtual_addr;
    context->spsr = 0;
    context->fpcr = 0;
    context->fpsr = 0;
    context->thread_pointer = 0;
    context->x[0] = startup->argument_count;
    context->x[1] = startup->arguments;
    context->x[2] = startup->environment;
    context->x[3] = pid;
    context->x[7] = startup->exec_payload;
    return context;
}

ThreadContextPtr ThreadContext__init_thread(KernelPtr kernel, StackPtr stack, UserAddress entry_point,
                                            Address start_function, Anything argument)
{
    ProcessStartup startup = ProcessStartup__new(0, 0, 0, 0, 0);
    ThreadContextPtr context = ThreadContext__init(kernel, 0, stack, entry_point, NULL_KERNEL_ADDRESS, &startup);
    context->x[0] = start_function;
    context->x[1] = argument;
    return context;
}

void ThreadContext__copy(ThreadContextPtr self, ThreadContextPtr source)
{
    *self = *source;
}

void ThreadContext__set_syscall_result(ThreadContextPtr self, SystemCallResult result)
{
    self->x[0] = result;
}

void ThreadContext__set_thread_pointer(ThreadContextPtr self, Address thread_pointer)
{
    self->thread_pointer = thread_pointer;
}

Address ThreadContext__thread_pointer(ThreadContextPtr self)
{
    return self->thread_pointer;
}

void ThreadContext__restart_syscall(ThreadContextPtr self)
{
    self->elr -= sizeof(uint32_t);
}

ThreadContextPtr ThreadContext__clone(KernelPtr kernel, ThreadContextPtr source)
{
    ThreadContextPtr context = v_ptr(Kernel__alloc(sizeof(ThreadContext)), ThreadContext);
    ThreadContext__copy(context, source);
    return context;
}

void ThreadContext__enter_signal(ThreadContextPtr self, ProcessSignal signal, Address handler, Address restorer)
{
    self->x[0] = signal;
    self->x[30] = restorer;
    self->elr = handler;
}
