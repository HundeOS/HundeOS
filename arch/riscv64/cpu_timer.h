#ifndef __RISCV64_CPU_TIMER_H
#define __RISCV64_CPU_TIMER_H

#include <stdbool.h>

#include "../../kernel/arch/process_context.h"

void RiscVCPUTimer__handle_interrupt(ProcessContextPtr context, bool current_privilege);

#endif
