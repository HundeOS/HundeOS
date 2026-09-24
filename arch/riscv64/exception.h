#ifndef __RISCV64_EXCEPTION_H
#define __RISCV64_EXCEPTION_H

#include "../../kernel/arch/exception.h"
#include "../../kernel/arch/process_context.h"
#include "process.h"

void Exception__install(void);
void Exception__handle(ProcessContextPtr context);

#endif
