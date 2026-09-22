#ifndef __RISCV64_EXCEPTION_H
#define __RISCV64_EXCEPTION_H

#include "../../kernel/arch/process_context.h"
#include "process.h"

void RiscVException__install(void);
void RiscVException__handle(ProcessContextPtr context);

#endif
