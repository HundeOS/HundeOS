#ifndef __RISCV64_EXCEPTION_H
#define __RISCV64_EXCEPTION_H

#include "../../kernel/arch/exception.h"
#include "../../kernel/arch/thread_context.h"
#include "process.h"

void Exception__install(void);
void Exception__handle(ThreadContextPtr context);

#endif
