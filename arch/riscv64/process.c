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

extern void RiscVProcess__switch_context(ProcessContextPtr context, Address page_table);
extern void RiscVProcess__switch_address_space(Address page_table);

ProcessContextPtr ProcessContext__init(KernelPtr kernel, ProcessID pid, StackPtr stack, UserAddress entry_point,
                                       KernelAddress page_table, ConstProcessStartupPtr startup)
{
    ProcessContextPtr context = v_ptr(Kernel__alloc(sizeof(ProcessContext)), ProcessContext);
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

void ProcessContext__copy(ProcessContextPtr self, ProcessContextPtr source)
{
    *self = *source;
}

void ProcessContext__set_syscall_result(ProcessContextPtr self, SystemCallResult result)
{
    self->x[10] = result;
}

void ProcessContext__restart_syscall(ProcessContextPtr self)
{
    self->sepc -= sizeof(uint32_t);
}

ProcessContextPtr ProcessContext__clone(KernelPtr kernel, ProcessContextPtr source)
{
    ProcessContextPtr context = v_ptr(Kernel__alloc(sizeof(ProcessContext)), ProcessContext);
    ProcessContext__copy(context, source);
    return context;
}

void ProcessContext__enter_signal(ProcessContextPtr self, ProcessSignal signal, Address handler, Address restorer)
{
    self->x[10] = signal;
    self->x[1] = restorer;
    self->sepc = handler;
}

void Process__set_syscall_result(ProcessPtr self, SystemCallResult result)
{
    ProcessContext__set_syscall_result(Process__required_context(self), result);
}

void Process__set_startup_stdio(ProcessPtr self, int64_t input, int64_t output, int64_t error)
{
    ProcessContextPtr context = Process__required_context(self);
    context->x[14] = input;
    context->x[15] = output;
    context->x[16] = error;
}

void ProcessContext__assert_user_irqs_unmasked(ProcessContextPtr self)
{
    if ((self->sstatus & RISCV_SSTATUS_SPP) != 0 || (self->sstatus & RISCV_SSTATUS_SPIE) == 0)
    {
        Kernel__panic("Invalid RV64 userspace interrupt state", self);
    }
}

static void Process__assert_context_ready(ProcessPtr self, ProcessContextPtr context, KernelAddress page_table)
{
    if (context->sepc == 0 || context->x[2] == 0 || (context->sstatus & RISCV_SSTATUS_SPP) != 0)
    {
        BString message = BString__sprintf(&invalid_context_format, self->pid, cstr(self->name), context, context->sepc,
                                           context->x[2], context->sstatus, paddr(page_table));
        Kernel__panic(cstr(message), context);
    }
}

__attribute__((noreturn)) void Process__switch_context(ProcessPtr self, ProcessContextPtr context,
                                                       KernelAddress page_table)
{
    Process__assert_context_ready(self, context, page_table);
    ProcessContext__assert_user_irqs_unmasked(context);
    RiscVProcess__switch_context(context, paddr(page_table));
    __builtin_unreachable();
}

void Process__switch_address_space(ProcessPtr self, ProcessContextPtr context, KernelAddress page_table)
{
    RiscVProcess__switch_address_space(paddr(page_table));
}

uint16_t Arch__elf_machine(void)
{
    return RISCV_ELF_MACHINE;
}
