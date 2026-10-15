#ifndef __KERNEL_IO_GICD_H
#define __KERNEL_IO_GICD_H
#include <stdint.h>
#include "../../../kernel/types/result.h"
#include "../../../kernel/arch/init_code.h"
#include "../../../kernel/system_info.h"

typedef struct GicInterrupt
{
    uint32_t irq;
    uint32_t acknowledgement;
} GicInterrupt;

typedef const GicInterrupt * ConstGicInterruptPtr;

static inline GicInterrupt GicInterrupt__new(uint32_t irq, uint32_t acknowledgement)
{
    return (GicInterrupt){.irq = irq, .acknowledgement = acknowledgement};
}

Result(InitCode) gic_enable_interrupt(InterruptControllerInfoPtr info, uint32_t irq);
void gic_mask_interrupt(InterruptControllerInfoPtr info, uint32_t irq);
void gic_unmask_interrupt(InterruptControllerInfoPtr info, uint32_t irq);
GicInterrupt gic_acknowledge_interrupt(InterruptControllerInfoPtr info);
void gic_complete_interrupt(InterruptControllerInfoPtr info, ConstGicInterruptPtr interrupt);
Result(InitCode) gic_init(InterruptControllerInfoPtr info);

#endif
