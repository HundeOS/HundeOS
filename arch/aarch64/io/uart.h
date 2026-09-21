#ifndef __IO_UART_H
#define __IO_UART_H

#include <stdint.h>
#include "../../../kernel/types/result.h"
#include "../../../kernel/arch/init_code.h"
#include "../../../kernel/system_info.h"

#include "pl011.h"

void uart_use_info(UartInfoPtr info);
Result(InitCode) uart_init(UartInfoPtr info);

void put_int(int c);
void puts(const char * s);

#endif
