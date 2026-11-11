#include "process.h"

#include <stddef.h>
#include <stdint.h>

#include "../../kernel/arch/process.h"
#include "../../kernel/debug.h"
#include "../../kernel/kernel_core.h"
#include "../../kernel/panic.h"
#include "../../kernel/process.h"
#include "page_table.h"

#define RISCV_SSTATUS_SPIE (1ULL << 5U)
#define RISCV_SSTATUS_SPP (1ULL << 8U)
#define RISCV_ELF_MACHINE 0xf3U

static BString invalid_context_format =
    bstr("Invalid RV64 userspace context pid=%i name=%s context=%p sepc=%p sp=%p sstatus=%p page_table=%p\n");

extern void RiscVProcess__switch_context(ThreadContextPtr context, Address page_table, ProcessPtr process);
extern void RiscVProcess__switch_address_space(Address page_table);

ThreadContextPtr ThreadContext__init(KernelPtr kernel, ProcessID pid, StackPtr stack, UserAddress entry_point,
                                     KernelAddress page_table, ConstProcessStartupPtr startup)
{
    ThreadContextPtr context = v_ptr(Kernel__alloc(sizeof(ThreadContext)), ThreadContext);
    for (uint64_t index = 0; index < 32; index++)
    {
        context->x[index] = 0;
    }

    context->x[2] = vaddr(stack->top);
    context->x[10] = startup->argument_count;
    context->x[11] = startup->arguments;
    context->x[12] = startup->environment;
    context->x[13] = pid;
    context->x[17] = startup->exec_payload;
    context->sepc = vaddr(entry_point);
    context->sstatus = RISCV_SSTATUS_SPIE;
    context->scause = 0;
    context->stval = 0;

    __d_printf(PROCESS, "RV64 context init context=%p sepc=%p sp=%p a0=%p a1=%p page_table=%p\n", context,
               context->sepc, context->x[2], context->x[10], context->x[11], paddr(page_table));
    return context;
}

ThreadContextPtr ThreadContext__init_thread(KernelPtr kernel, StackPtr stack, UserAddress entry_point,
                                            Address start_function, Anything argument)
{
    ProcessStartup startup = ProcessStartup__new(0, 0, 0, 0, 0);
    ThreadContextPtr context = ThreadContext__init(kernel, 0, stack, entry_point, NULL_KERNEL_ADDRESS, &startup);
    context->x[10] = start_function;
    context->x[11] = argument;
    return context;
}

void ThreadContext__copy(ThreadContextPtr self, ThreadContextPtr source)
{
    *self = *source;
}

void ThreadContext__set_syscall_result(ThreadContextPtr self, SystemCallResult result)
{
    self->x[10] = result;
}

void ThreadContext__set_thread_pointer(ThreadContextPtr self, Address thread_pointer)
{
    self->x[4] = thread_pointer;
}

Address ThreadContext__thread_pointer(ThreadContextPtr self)
{
    return self->x[4];
}

void ThreadContext__restart_syscall(ThreadContextPtr self)
{
    self->sepc -= sizeof(uint32_t);
}

ThreadContextPtr ThreadContext__clone(KernelPtr kernel, ThreadContextPtr source)
{
    ThreadContextPtr context = v_ptr(Kernel__alloc(sizeof(ThreadContext)), ThreadContext);
    ThreadContext__copy(context, source);
    return context;
}

void ThreadContext__enter_signal(ThreadContextPtr self, ProcessSignal signal, Address handler, Address restorer)
{
    self->x[10] = signal;
    self->x[1] = restorer;
    self->sepc = handler;
}

void Process__set_syscall_result(ProcessPtr self, SystemCallResult result)
{
    ThreadContext__set_syscall_result(Process__required_context(self), result);
}

void Process__set_startup_stdio(ProcessPtr self, int64_t input, int64_t output, int64_t error)
{
    ThreadContextPtr context = Process__required_context(self);
    context->x[14] = input;
    context->x[15] = output;
    context->x[16] = error;
}

void ThreadContext__assert_user_irqs_unmasked(ThreadContextPtr self)
{
    if ((self->sstatus & RISCV_SSTATUS_SPP) != 0 || (self->sstatus & RISCV_SSTATUS_SPIE) == 0)
    {
        Kernel__panic("Invalid RV64 userspace interrupt state", self);
    }
}

static void Process__assert_context_ready(ProcessPtr self, ThreadContextPtr context, KernelAddress page_table)
{
    if (context->sepc == 0 || context->x[2] == 0 || (context->sstatus & RISCV_SSTATUS_SPP) != 0)
    {
        BString message = BString__sprintf(&invalid_context_format, self->pid, cstr(self->name), context, context->sepc,
                                           context->x[2], context->sstatus, paddr(page_table));
        Kernel__panic(cstr(message), context);
    }
}

__attribute__((noreturn)) void Process__switch_context(ProcessPtr self, ThreadContextPtr context,
                                                       KernelAddress page_table)
{
    Process__assert_context_ready(self, context, page_table);
    ThreadContext__assert_user_irqs_unmasked(context);
    RiscVProcess__switch_context(context, paddr(page_table), self);
    __builtin_unreachable();
}

void Process__switch_address_space(ProcessPtr self, ThreadContextPtr context, KernelAddress page_table)
{
    RiscVProcess__switch_address_space(paddr(page_table));
    Process__address_space_switched(self);
}

uint16_t Arch__elf_machine(void)
{
    return RISCV_ELF_MACHINE;
}
