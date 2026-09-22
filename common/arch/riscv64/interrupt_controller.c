#include "interrupts/interrupt_controller.h"

#define PLIC_SUPERVISOR_CONTEXT_ENABLE_OFFSET 0x2080ULL

void InterruptController__unmask(Address distributor, IRQNumber irq)
{
    uint64_t register_index = irq / 32;
    uint64_t bit_index = irq % 32;
    volatile uint32_t * enable =
        (volatile uint32_t *)(distributor + PLIC_SUPERVISOR_CONTEXT_ENABLE_OFFSET + register_index * sizeof(uint32_t));
    *enable |= 1U << bit_index;
    __asm__ volatile("fence iorw, iorw" ::: "memory");
}
