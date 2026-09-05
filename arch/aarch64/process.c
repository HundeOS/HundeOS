#include "process.h"
#include "page_table.h"

#include <stddef.h>
#include <stdint.h>

#include "asm_helpers.h"
#include "../../kernel/process.h"
#include "../../kernel/arch/cpu.h"
#include "../../kernel/panic.h"

#include <stdint.h>


static BString invalid_context_format = bstr(
    "Invalid userspace context pid=%i name=%s context=%p elr=%p sp=%p spsr=%p page_table=%p\n"
);

/* Build SPSR for entering EL0t.
 *  M[3:0]=0000 (EL0t)
 *  DAIF: 0 = unmask (enable) Debug/SError/IRQ/FIQ on entry
 *        set bits if you want them masked initially.
 */
inline uint64_t Process__spsr_for_el0(unsigned mask_irqs /*bits [9:6]*/) {
    uint64_t spsr = 0;
    // EL0t mode (M[3:0] = 0b0000)
    spsr |= (0b0000);
    // Mask FIQ, IRQ, SError
    spsr |= (1UL << 6) | (1UL << 7) | (1UL << 8);
    // If you want to unmask IRQs for user processes, clear bit 7
    if (!mask_irqs)
        spsr &= ~(1UL << 7);
    return spsr;
}

ProcessContextPtr ProcessContext__init(
    KernelPtr kernel,
    ProcessID pid,
    StackPtr stack,
    UserAddress entry_point,
    KernelAddress page_table,
    uint64_t argc,
    Address argv
)
{
    KernelAddress context_addr = Kernel__alloc(sizeof(ProcessContext));
    ProcessContextPtr context = v_ptr(context_addr, ProcessContext);

    __d_printf(PROCESS, "ProcessContext init context=%p\n", context);

    context->sp = vaddr(stack->top);

    for (int i = 0; i < 31; i++)
    {
        context->x[i] = 0;
    }
    for (int i = 0; i < 32; i++)
    {
        context->q[i] = 0;
    }
    context->fpcr = 0;
    context->fpsr = 0;

    // context->spsr = Process__spsr_for_el0(0);
    context->spsr = 0x0;

    context->elr = vaddr(entry_point);

    context->x[0] = argc;
    context->x[1] = argv;
    context->x[8] = vaddr(entry_point);

    __d_printf(
        PROCESS,
        "ProcessContext__init done context=%p elr=%p sp=%p x0=%p x1=%p x8=%p page_table=%p\n",
        context,
        context->elr,
        context->sp,
        context->x[0],
        context->x[1],
        context->x[8],
        paddr(page_table)
    );
    return context;
}

void ProcessContext__dispose(ProcessContextPtr self)
{
    Kernel__free(addr(self));
}

extern void asm_switch_context(ProcessContextPtr context, Address pagetable);
extern void asm_switch_kernel_context(ProcessContextPtr context, Address pagetable);

void Process__set_syscall_result(ProcessPtr self, SystemCallResult result)
{
    ProcessContextPtr context = self->context;
    context->x[0] = result;
}

void ProcessContext__assert_user_irqs_unmasked(ProcessContextPtr self)
{
    uint64_t mode = self->spsr & 0xf;
    uint64_t irq_masked = self->spsr & (1UL << 7);

    if (irq_masked)
    {
        __d_printf(
            FAULT,
            "Refusing to return to userspace with IRQs masked: context=%p elr=%p sp=%p spsr=%p mode=%p irq_masked=%p current_daif=%p ttbr0=%p\n",
            self,
            self->elr,
            self->sp,
            self->spsr,
            mode,
            irq_masked,
            read_daifclr(),
            read_ttbr0_el1()
        );
        Kernel__panic("Return to userspace with IRQs masked", self);
    }
}

static void Process__assert_context_ready(ProcessPtr self)
{
    ProcessContextPtr context = self->context;
    uint64_t mode = context->spsr & 0xf;

    if (context->elr == 0 || context->sp == 0 || mode != 0)
    {
        BString message = BString__sprintf(
            &invalid_context_format,
            self->pid,
            cstr(self->name),
            context,
            context->elr,
            context->sp,
            context->spsr,
            paddr(ProcessPageTable__root(&self->page_table))
        );
        Kernel__panic(cstr(message), context);
    }
}

__attribute__((noreturn))
void Process__context_switch(ProcessPtr self)
{
    ProcessContextPtr context = self->context;

    const char * name = cstr(self->name);
    __d_printf(PROCESS_SWITCH, "Process context switch name=%s sp=%p tbbr0=%p spsr_el1=%p x0=%p x1=%p x8=%p elr=%p\n",
        name,
        self->context->sp,
        paddr(ProcessPageTable__root(&self->page_table)),
        self->context->spsr,
        self->context->x[0],
        self->context->x[1],
        self->context->x[8],
        self->context->elr);

    Address base = addr(context);
    __d_printf(PROCESS_SWITCH,
        "ProcessContext offsets from context=%p sp=%i x0=%i q0=%i elr=%i spsr=%i fpcr=%i fpsr=%i size=%i\n",
        context,
        (int) (addr(&context->sp) - base),
        (int) (addr(&context->x) - base),
        (int) (addr(&context->q) - base),
        (int) (addr(&context->elr) - base),
        (int) (addr(&context->spsr) - base),
        (int) (addr(&context->fpcr) - base),
        (int) (addr(&context->fpsr) - base),
        (int) sizeof(ProcessContext));

    __d_printf(
        PROCESS_SWITCH,
        "Process__context_switch asm pid=%i name=%s context=%p elr=%p sp=%p x8=%p page_table=%p\n",
        self->pid,
        name,
        context,
        context->elr,
        context->sp,
        context->x[8],
        paddr(ProcessPageTable__root(&self->page_table))
    );
    Process__assert_context_ready(self);
    ProcessContext__assert_user_irqs_unmasked(context);
    __d_println(PROCESS_SWITCH, "Irqs OK");
    asm_switch_context(context, paddr(ProcessPageTable__root(&self->page_table)));
    __builtin_unreachable();
}

void Process__activate_address_space(ProcessPtr self)
{
    ProcessContextPtr context = self->context;
    asm_switch_kernel_context(context, paddr(ProcessPageTable__root(&self->page_table)));
}

void Process__save_context(ProcessPtr self, ProcessContextPtr context)
{
    if (self->context == NULL)
    {
        Kernel__panic("Process context is NULL", context);
    }
    else if (self->state != PROC_STATE_RUNNING)
    {
        BString format = BString__new("Process not running state=%s pid=%i name=%s\n");
        BString message = BString__sprintf(&format, ProcessState__to_string(self->state), self->pid, cstr(self->name));

        Kernel__panic(cstr(message), context);
    }
    else if (context != NULL)
    {
        __d_printf(PROCESS, "Process context save self=%p context=%p %i\n", self->context, context, self->pid);
        *self->context = *context;
    }
    else
    {
        __d_printf(PROCESS, "Process context ignoring save self=%p context=NULL %i\n", self->context, self->pid);
    }
}
