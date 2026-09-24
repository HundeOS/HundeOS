#include "gicd.h"
#include "../../../kernel/types/result.h"
#include "../../../kernel/arch/init.h"
#include "../../../kernel/debug.h"
#include "../../../kernel/system_info.h"
#include "../asm_helpers.h"

#define GICD_CTLR_ENABLE_GRP1NS (1u << 1)
#define GICD_CTLR_ARE_NS (1u << 4)
#define GIC_SPI_START 32u

static Address gicd_base(InterruptControllerInfoPtr info)
{
    return info->details.gic.distributor_virtual_base;
}

static void gicd_enable_group1ns(InterruptControllerInfoPtr info)
{
    volatile uint32_t * ctlr = (uint32_t *)(gicd_base(info) + 0x000);
    *ctlr |= GICD_CTLR_ENABLE_GRP1NS;
    __asm__ volatile("dsb sy; isb" ::: "memory");
}

static void gicd_route_spi_to_cpu0(InterruptControllerInfoPtr info, uint32_t irq)
{
    if (irq < GIC_SPI_START)
    {
        return;
    }

    volatile uint64_t * router = (uint64_t *)(gicd_base(info) + 0x6000 + (8 * irq));
    *router = 0;
    __asm__ volatile("dsb sy; isb" ::: "memory");
}

Result(InitCode) gic_enable_interrupt(InterruptControllerInfoPtr info, uint32_t irq)
{
    unsigned int reg = irq / 32;
    unsigned int bit = irq % 32;
    Address base = gicd_base(info);

    volatile unsigned int * GICD_ISENABLER = (unsigned int *)(base + 0x100 + (reg * 4));
    volatile uint8_t * GICD_IPRIORITYR = (uint8_t *)(base + 0x400 + irq);
    volatile uint8_t * GICD_ITARGETSR = (uint8_t *)(base + 0x800 + irq);
    volatile uint32_t * GICD_IGROUPR = (uint32_t *)(base + 0x080 + (reg * 4));

    gicd_enable_group1ns(info);
    *GICD_IPRIORITYR = 0xA0; // medium priority
    *GICD_ITARGETSR = 1;     // legacy route to CPU0
    gicd_route_spi_to_cpu0(info, irq);
    *GICD_ISENABLER = (1 << bit);
    *GICD_IGROUPR |= (1 << bit);

    __d_printf(IRQ, "gic enable irq=%i reg=%i bit=%i isenabler=0x%x igroupr=0x%x ctlr=0x%x\n", irq, reg, bit,
               *GICD_ISENABLER, *GICD_IGROUPR, *(volatile uint32_t *)(base + 0x000));

    __d_printf(IRQ2, "ISENABLER1=%x\n", *(volatile uint32_t *)(base + 0x104));
    __d_printf(IRQ2, "GICD_CTLR=%x\n", *(volatile uint32_t *)(base + 0x000));
    __d_printf(IRQ2, "daif=%x\n", read_daifclr());
    __d_printf(IRQ2, "GICD_IGROUPR=%x\n", *GICD_IGROUPR);
    __d_printf(IRQ2, "icc_igrpen1=%x\n", read_icc_igrpen1());

    return Success(InitCode, ARCH_INIT_OK);
}

void gic_mask_interrupt(InterruptControllerInfoPtr info, uint32_t irq)
{
    unsigned int reg = irq / 32;
    unsigned int bit = irq % 32;
    volatile uint32_t * GICD_ICENABLER = (uint32_t *)(gicd_base(info) + 0x180 + (reg * 4));
    *GICD_ICENABLER = (1u << bit);
    __asm__ volatile("dsb sy; isb" ::: "memory");
    __d_printf(IRQ, "gic mask irq=%i reg=%i bit=%i\n", irq, reg, bit);
}

void gic_unmask_interrupt(InterruptControllerInfoPtr info, uint32_t irq)
{
    unsigned int reg = irq / 32;
    unsigned int bit = irq % 32;
    volatile uint32_t * GICD_ISENABLER = (uint32_t *)(gicd_base(info) + 0x100 + (reg * 4));
    *GICD_ISENABLER = (1u << bit);
    __asm__ volatile("dsb sy; isb" ::: "memory");
    __d_printf(IRQ, "gic unmask irq=%i reg=%i bit=%i isenabler=0x%x\n", irq, reg, bit, *GICD_ISENABLER);
}

Result(InitCode) gic_init(InterruptControllerInfoPtr info)
{
    // Enable Distributor
    gicd_enable_group1ns(info);

    if (info->kind != INTERRUPT_CONTROLLER_GIC_V3)
    {
        return Success(InitCode, ARCH_INIT_OK);
    }

    // Enable CPU interface via system registers (GICv3)
    asm volatile("msr ICC_PMR_EL1, %0" ::"r"(0xFF));  // accept all priorities
    asm volatile("msr ICC_IGRPEN1_EL1, %0" ::"r"(1)); // enable group1
    asm volatile("msr DAIFClr, #2" ::: "memory");     // enable group1
    asm volatile("isb; dsb sy");
    __d_printf(IRQ2, "daif=%x\n", read_daifclr());
    __d_printf(IRQ2, "current_el=%x\n", (read_current_el() >> 2));
    __d_printf(IRQ2, "spsel=%x\n", read_spsel());
    // __d_printf(IRQ2, "hcr_el2=%x\n", read_hcr_el2());

    return Success(InitCode, ARCH_INIT_OK);
}
