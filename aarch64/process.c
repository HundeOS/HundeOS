#include "process.h"
#include "page_table.h"

#include <stddef.h>
#include <stdint.h>
#include <stdint.h>
#include <stdint.h>
#include <stdint.h>

#define STACK_SIZE 1024 * 512 // 512 k words

Process process_table[2048];

uint64_t stacks[MAX_PROCESSES][STACK_SIZE];

static bool initialized = false;

static mutex_t table_lock;

void * Stack__clear(ProcessId pid) {
    for (uint64_t i = 0; i < STACK_SIZE; i++) {
        stacks[pid][i] = 0;
    }
    return stacks[pid];
}

Process * Process__clear(Process * proc, ProcessId pid) {
    proc->pid = pid;
    proc->stack.base = Stack__clear(pid);
    proc->stack.size = STACK_SIZE;
    proc->state = PROC_STATE_UNALLOCATED;
    proc->page_table = PageTable__new();
    proc->entry_point = NULL;

    return proc;
}

void Process__init_table() {
    for (int num = 0; num < MAX_PROCESSES; num++) {
        Process__clear(&process_table[num], num);
    }
}

Process * Process__next() {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].state == PROC_STATE_UNALLOCATED) {
            return &process_table[i];
        }
    }
    return NULL;
}

Process * Process__new(ProcessEntryPoint entry_point) {
    mutex_lock(&table_lock);
    if (initialized) {
        Process__init_table();
    }
    Process * proc = Process__next();
    proc->entry_point = entry_point;
    proc->state = PROC_STATE_NEW;
    mutex_unlock(&table_lock);

    return proc;
}

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
__attribute__((noreturn))
inline void Process__enter_el0(Process * proc,
                               int mask_irqs_on_entry)  // 1 => keep IRQs masked at EL0
{
    register uint64_t x0 __asm__("x0") = 0;
    uint64_t spsr = Process__spsr_for_el0(mask_irqs_on_entry ? 0b0111 : 0); // mask D,A,I if desired

    __asm__ __volatile__(
        // Set user stack pointer (EL0)
        "msr sp_el0,  %1\n"
        // Next PC on ERET
        "msr elr_el1, %2\n"
        // PSTATE on entry to EL0
        "msr spsr_el1, %3\n"
        "isb\n"
        // x0 is already loaded
        "eret\n"
        :
        : "r"(x0), "r"(proc->stack.base), "r"(proc->entry_point), "r"(spsr)
        : "memory");
    __builtin_unreachable();
}

uint64_t Process__start(Process * self) {
    self->state = PROC_STATE_RUNNING;
    Process__enter_el0(self, 0);
}