#include "process.h"
#include "page_table.h"

#include <stddef.h>
#include <stdint.h>

#include "asm_helpers.h"
#include "../../kernel/process.h"


/* Build SPSR for entering EL0t.
 *  M[3:0]=0000 (EL0t)
 *  DAIF: 0 = unmask (enable) Debug/SError/IRQ/FIQ on entry
 *        set bits if you want them masked initially.
 */
inline uint64_t Process__spsr_for_el0(unsigned daif_mask /*bits [9:6]*/) {
    const uint64_t M_EL0t = 0x0ull;
    return M_EL0t | ((uint64_t)(daif_mask & 0xF) << 6);
}

/* Enter EL0 with:
 *   PC = user_entry_va
 *   SP_EL0 = user_stack_top_va
 *   x0 = arg0 (you can extend to x1..x7 similarly)
 */
// __attribute__((noreturn))
void Process__enter_el0(Process * self,
                        bool mask_irqs_on_entry)  // 1 => keep IRQs masked at EL0
{
    uint64_t spsr = Process__spsr_for_el0(mask_irqs_on_entry ? 0b0111 : 0); // mask D,A,I if desired
    __d_println(FOCUS, "setting pagetable");
    write_ttbr0_el1(self->page_table.phys_addr);
    __d_println(FOCUS, "setting el0 sp");
    write_sp_el0(self->stack.top.virtual_addr);
    __d_println(FOCUS, "setting el0 entrypoint");
    write_elr_el1(self->entry_point.virtual_addr);
    __d_println(FOCUS, "switch to el0");
    write_spsr_el1(spsr);
    isb();
    eret();

    __builtin_unreachable();
}

uint64_t Process__start(Process * self) {
    self->state = PROC_STATE_RUNNING;
    __d_println(FOCUS, "Process entering el0");
    Process__enter_el0(self, 0);
}