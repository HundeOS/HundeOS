#ifndef __AARCH64_CPU_TIMER_H
#define __AARCH64_CPU_TIMER_H

// PPI numbers
#define PPI_CNTV   27  // virtual timer IRQ
#define PPI_CNTP   30  // physical non-secure timer IRQ
#include <stdint.h>

#include "../../kernel/irq/irq_dispatcher.h"
#include "../../kernel/system_info.h"

void CPUTimer__start(SystemInfoPtr system_info, uint64_t hz);
Result(IRQDispatchStatus) CPUTimer__handle_irq(IRQDispatchContextPtr context, void * data);

#endif
