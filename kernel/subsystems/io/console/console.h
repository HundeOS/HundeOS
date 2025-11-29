#ifndef __KERNEL_SUBSYSTEMS_IO_CONSOLE_CONSOLE_H
#define __KERNEL_SUBSYSTEMS_IO_CONSOLE_CONSOLE_H
#include <stdint.h>

#include "../io.h"
#include "../io_subsystem.h"

#include "../../../types/optional.h"

FixedArrayDefine(IOContextPtr, 1024, ParkedContexts, Ptr__equals);

typedef struct ConsoleState
{
    IOState state; // must be the first member as we rely on this for inheritance
    IOBuffer read_buffer;
    IOBuffer write_buffer;
    IOInternalHandle handle_counter;

    ParkedContexts parked_contexts;

    mutex_t lock;
} ConsoleState;

typedef ConsoleState * ConsoleStatePtr;

ConsoleStatePtr ConsoleState__instance();
IOPtr Console__heap_new();

void Console__IRQ_handler(ConsoleStatePtr self, Char data);

#endif
