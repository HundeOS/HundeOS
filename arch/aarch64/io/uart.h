#ifndef __IO_UART_H
#define __IO_UART_H

#include <stdint.h>
#include "../../../kernel/subsystems/io/io.h"
#include "../../../kernel/types/result.h"
#include "../../../kernel/arch/init.h"
#include "../../../kernel/system_info.h"

#include "pl011.h"

void uart_use_info(UartInfoPtr info);
Result(InitCode) uart_init(UartInfoPtr info);
Result(InitCode) uart_enable_irqs(UartInfoPtr info);

void put_int(int c);
void puts(const char * s);

void uart_irq_handler(IOSubsystemPtr io_subsystem);

// Result(ByteCount) read_line(BufferPtr buffer);

#endif
