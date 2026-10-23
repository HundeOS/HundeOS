#include "interrupts/interrupt_controller.h"
#include "plic.h"

void InterruptController__unmask(Address distributor, IRQNumber irq)
{
    Plic__set_masked(distributor, irq, false);
    __asm__ volatile("fence iorw, iorw" ::: "memory");
}
