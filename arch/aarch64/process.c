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
inline uint64_t Process__spsr_for_el0(unsigned daif_mask /*bits [9:6]*/) {
    const uint64_t M_EL0t = 0x0ull;
    return (0b000 << 6) | (0b00 << 2) | 0b0000 | M_EL0t | ((uint64_t)(daif_mask & 0xF) << 6);
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

ProcessContextPtr ProcessContext__init(StackPtr stack, UserAddress entry_point, KernelAddress page_table)
{
    ProcessContextPtr context = k_v_ptr(stack->top, ProcessContext);
    context->sp = vaddr(stack->top);

    for (int i = 0; i < 31; i++)
    {
        context->x[i] = 0;
        context->q[i] = 0;
    }

    context->spsr = Process__spsr_for_el0(0);

    UserAddress trampoline = Process__el1_trampoline();
    PageTable__map(v_ptr(page_table, PageTable),
        trampoline,
        PAGE_SIZE * 4);

    context->elr = vaddr(trampoline);

    uint64_t argc = 0;
    char ** argv = NULL;
    context->x[0] = argc;
    context->x[1] = (uint64_t) argv;
    context->x[8] = vaddr(entry_point);

    return context;
}

void Process__switch_context(ProcessContextPtr context)
{

}

__attribute__((noreturn))
void Process__enter_el0(Process * self,
                        bool mask_irqs_on_entry)  // 1 => keep IRQs masked at EL0
{
    UserAddress trampoline = Process__el1_trampoline();
    PageTable__map(v_ptr(self->page_table, PageTable),
        trampoline,
        PAGE_SIZE * 4);

    uint64_t spsr = Process__spsr_for_el0(mask_irqs_on_entry ? 0b0111 : 0); // mask D,A,I if desired
    uint64_t argc = 0;
    char ** argv = NULL;

    __d_printf(DEBUG,
        "Process entering el0 sp=%p ep=%p pt=%p\n",
        self->stack.top.virtual_addr,
        self->entry_point.virtual_addr,
        self->page_table.phys_addr);

    __asm__ volatile(
            "msr sp_el0, %0          \n"

            // 2) install TTBR0_EL1 (process page table) + TLB maintenance
            "dsb ishst               \n"
            "msr ttbr0_el1, %1       \n"
            "dsb ish                 \n"
            "isb                     \n"
            "tlbi vmalle1            \n"
            "dsb ish                 \n"
            "isb                     \n"

            // 3) program ELR_EL1 with trampoline user entry VA
            "msr elr_el1, %2         \n"

            // 4) program SPSR_EL1 for EL0t
            "msr spsr_el1, %3        \n"
            "isb                     \n"

            // 5) preload argument registers that EL0 code expects
            "mov x8, %4              \n"
            "mov x0, %5              \n"
            "mov x1, %6              \n"
            "msr daifclr, #0xf       \n" // unmask all exceptions
            // 6) transition EL1->EL0t and start executing at ELR_EL1
            "eret                    \n"
            ::
           "r"(vaddr(self->stack.top)),
           "r"(paddr(self->page_table)),
           "r"(vaddr(trampoline)),
           "r"(spsr),
           "r"(vaddr(self->entry_point)),
           "r"(argc),
           "r"(argv)
           : "memory");
    __d_println(PANIC, "Process exited");
    __builtin_unreachable();
}

uint64_t Process__start(Process * self) {
    self->state = PROC_STATE_RUNNING;
    __d_println(DEBUG, "Process entering el0");
    Process__enter_el0(self, 0);
}