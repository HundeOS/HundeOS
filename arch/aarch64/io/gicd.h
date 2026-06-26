#ifndef __KERNEL_IO_GICD_H
#define __KERNEL_IO_GICD_H
#include <stdint.h>
#include "../../../kernel/types/result.h"
#include "../../../kernel/arch/init.h"

extern const uint64_t __gicd_start[];

#define GICR_OFFSET 0xA0000UL

#define GICD_BASE           (uint64_t)&__gicd_start // virtual base of GICD
#define GICD_CTLR           (*(volatile unsigned int *)(GICD_BASE + 0x000))
#define GICD_ISENABLER(n)   (*(volatile unsigned int *)(GICD_BASE + 0x100 + ((n) * 4)))
#define GICD_IPRIORITYR(n)  (*(volatile unsigned int *)(GICD_BASE + 0x400 + ((n) * 4)))
#define GICD_ITARGETSR(n)   (*(volatile unsigned int *)(GICD_BASE + 0x800 + ((n) * 4)))

#define GICR_BASE   (GICD_BASE + GICR_OFFSET)
#define GICR_CTLR   (*(volatile unsigned int *)(GICR_BASE + 0x0000))
#define GICR_WAKER  (*(volatile unsigned int *)(GICR_BASE + 0x0014))

#define GICC_PMR    (*(volatile unsigned int *)(0x08010000UL + 0x004)) // priority mask register

Result(InitCode) gic_enable_interrupt(uint32_t irq);
void gic_mask_interrupt(uint32_t irq);
void gic_unmask_interrupt(uint32_t irq);
Result(InitCode) gic_init();

#endif
