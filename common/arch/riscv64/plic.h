#ifndef HUND_RISCV64_PLIC_H
#define HUND_RISCV64_PLIC_H

#include <stdbool.h>
#include <stdint.h>
#include "interrupts/interrupt_types.h"
#include "types/address.h"

typedef volatile uint32_t * PlicPriorityRegisterPtr;

static inline void Plic__set_masked(Address distributor, IRQNumber irq, bool masked)
{
    PlicPriorityRegisterPtr priority = (PlicPriorityRegisterPtr)(distributor + irq * sizeof(uint32_t));
    *priority = masked ? 0U : 1U;
}

#endif
