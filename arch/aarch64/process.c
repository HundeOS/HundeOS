#include "process.h"
#include "page_table.h"

#include <stddef.h>
#include <stdint.h>

#include "asm_helpers.h"
#include "../../kernel/process.h"

#include <stdint.h>


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

extern char __el0_entry[];
extern char __user_jump_start[];
extern char __user_jump_end[];
extern char __user_jump_phys_start[];
extern char __user_jump_phys_end[];

const Address TRAMP_ADDR = 0x10000;

static UserAddress Process__el1_trampoline()
{
    Offset tramp_offset = addr_of(__el0_entry) - addr_of(__user_jump_start);
    __d_printf(DEBUG, "Trampoline v=%p u=%p off=%p\n", addr_of(__el0_entry), addr_of(__user_jump_start), tramp_offset);

    // we need to map the trampoline
    return (UserAddress) {
        .virtual_addr = TRAMP_ADDR,
        .kernel_addr = (KernelAddress) {
            .virtual_addr = addr_of(__user_jump_start),
            .phys_addr = addr_of(__user_jump_phys_start),
            .valid = true,
        },
        .read = true,
        .write = false,
        .execute = true,
        .valid = true,
    };
}

ProcessContextPtr ProcessContext__init(
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

    UserAddress trampoline = Process__el1_trampoline();
    PageTable__map(v_ptr(page_table, PageTable), trampoline, PAGE_SIZE * 4);

    context->elr = vaddr(trampoline);

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

extern void asm_switch_context(ProcessContextPtr context, Address pagetable);
extern void asm_switch_kernel_context(ProcessContextPtr context, Address pagetable);

__attribute__((noreturn))
void Process__context_switch(ProcessPtr self)
{
    ProcessContextPtr context = self->context;

    const char * name = cstr(self->name);
    __d_printf(PROCESS, "Process context switch name=%s sp=%p tbbr0=%p spsr_el1=%x x0=%p x1=%p x8=%p elr=%p\n",
        name,
        self->context->sp,
        paddr(self->page_table),
        self->context->spsr,
        self->context->x[0],
        self->context->x[1],
        self->context->x[8],
        self->context->elr);

    Address base = addr(context);
    __d_printf(PROCESS,
        "ProcessContext offsets from context(%p) sp=%p x[0]=%p q[0]=%p elr=%p spsr=%p fpcr=%p fpsr=%p size=%p\n",
        context,
        addr(&context->sp) - base,
        addr(&context->x) - base,
        addr(&context->q) - base,
        addr(&context->elr) - base,
        addr(&context->spsr) - base,
        addr(&context->fpcr) - base,
        addr(&context->fpsr) - base,
        sizeof(ProcessContext));

    // do we have suspended work to finish on the
    // kernel side?
    Match(ParkedKernelContextPtr, self->parked_context)
        IsSome(parked_kernel_context,
        {
            Match(Continuation, parked_kernel_context->continuation)
                IsSome(continuation,
                {
                    __d_println(PROCESS, "Continuation kernel context switch");
                    asm_switch_kernel_context(context, paddr(self->page_table));

                    __d_printf(PROCESS, "Continuation calling %p %p", continuation, parked_kernel_context);
                    Result(Anything) res = continuation(parked_kernel_context);
                    self->parked_context = None(ParkedKernelContextPtr);
                    ResultMatch(Anything, res)
                        IsSuccess(result_val,
                        {
                            __d_println(PROCESS, "Set syscall result");
                            context->x[0] = result_val;
                        });
                        IsError(code, message,
                        {
                            context->x[0] = code;
                            __d_printf(PANIC, "Continuation error: %s\n", cstr(message));
                        })
                    End

                })
            End
        })
    End
    __d_printf(
        PROCESS,
        "Process__context_switch asm pid=%i name=%s context=%p elr=%p sp=%p x8=%p page_table=%p\n",
        self->pid,
        name,
        context,
        context->elr,
        context->sp,
        context->x[8],
        paddr(self->page_table)
    );
    asm_switch_context(context, paddr(self->page_table));
    __builtin_unreachable();
}

void Process__save_context(ProcessPtr self, ProcessContextPtr context)
{
    if (self->context == NULL)
    {
        Kernel__panic("Process context is NULL", context);
    }
    else if (self->state != PROC_STATE_RUNNING)
    {
        KString format = KString__new("Process not running state=%s pid=%i name=%s\n");
        KString message = KString__sprintf(&format, ProcessState__to_string(self->state), self->pid, cstr(self->name));

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
