#ifndef __KERNEL_IO_GICD_H
#define __KERNEL_IO_GICD_H
#include <stdint.h>
#include "../../../kernel/types/result.h"
#include "../../../kernel/arch/init.h"
#include "../../../kernel/system_info.h"

Result(InitCode) gic_enable_interrupt(InterruptControllerInfoPtr info, uint32_t irq);
void gic_mask_interrupt(InterruptControllerInfoPtr info, uint32_t irq);
void gic_unmask_interrupt(InterruptControllerInfoPtr info, uint32_t irq);
Result(InitCode) gic_init(InterruptControllerInfoPtr info);

#endif
