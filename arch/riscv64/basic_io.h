#ifndef HUND_RISCV64_BASIC_IO_H
#define HUND_RISCV64_BASIC_IO_H

#include "../../kernel/system_info.h"

void RiscVBasicIO__configure_console(ConstUartInfoPtr uart);
void RiscVBasicIO__configure_trace(ConstUartInfoPtr uart);

#endif
