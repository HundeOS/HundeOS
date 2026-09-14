#include "interrupts/interrupt_controller.h"

void InterruptController__unmask(Address distributor, IRQNumber irq)
{
    uint64_t register_index = irq / 32;
    uint64_t bit_index = irq % 32;
    volatile uint32_t * enable = (volatile uint32_t *)(distributor + 0x100 + register_index * sizeof(uint32_t));
    *enable = 1U << bit_index;
    __asm__ volatile("dsb sy; isb" ::: "memory");
}
