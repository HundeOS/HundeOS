#include "gicd.h"
#include "../../../kernel/types/result.h"
#include "../../../kernel/arch/init.h"
#include "../../../kernel/debug.h"
#include "../asm_helpers.h"

Result(InitCode) gic_enable_interrupt(uint32_t irq)
{
    unsigned int reg = irq / 32;
    unsigned int bit = irq % 32;

    volatile unsigned int *GICD_ISENABLER = (unsigned int *)(GICD_BASE + 0x100 + (reg * 4));
    volatile uint8_t *GICD_IPRIORITYR = (uint8_t *)(GICD_BASE + 0x400 + irq);
    volatile uint8_t *GICD_ITARGETSR = (uint8_t *)(GICD_BASE + 0x800 + irq);
    volatile uint32_t *GICD_IGROUPR1 = (uint32_t *)(GICD_BASE + 0x084);

    *(volatile uint32_t *)(GICD_BASE + 0x000) = 1;
    *GICD_IPRIORITYR = 0xA0;  // medium priority
    *GICD_ITARGETSR = 1;      // route to CPU0
    *GICD_ISENABLER = (1 << bit);
    *GICD_IGROUPR1 |= (1 << 1);

    __d_printf(IRQ2, "ISENABLER1=%x\n", *(volatile uint32_t *)(GICD_BASE + 0x104));
    __d_printf(IRQ2, "GICD_CTLR=%x\n", *(volatile uint32_t *)(GICD_BASE + 0x000));
    __d_printf(IRQ2, "daif=%x\n", read_daifclr());
    __d_printf(IRQ2, "GICD_IGROUPR1=%x\n", *GICD_IGROUPR1);
    __d_printf(IRQ2, "icc_igrpen1=%x\n", read_icc_igrpen1());

    return Success(InitCode, ARCH_INIT_OK);
}

Result(InitCode) gic_init(void)
{
    // Enable Distributor
    GICD_CTLR = 1;

    // Enable CPU interface via system registers (GICv3)
    asm volatile("msr ICC_PMR_EL1, %0" :: "r"(0xFF));   // accept all priorities
    asm volatile("msr ICC_IGRPEN1_EL1, %0" :: "r"(1));  // enable group1
    asm volatile("msr DAIFClr, #2" :::"memory" );  // enable group1
    asm volatile("isb; dsb sy");
    __d_printf(IRQ2, "daif=%x\n", read_daifclr());
    __d_printf(IRQ2, "current_el=%x\n", (read_current_el() >> 2));
    __d_printf(IRQ2, "spsel=%x\n", read_spsel());
    // __d_printf(IRQ2, "hcr_el2=%x\n", read_hcr_el2());

    return Success(InitCode, ARCH_INIT_OK);
}
