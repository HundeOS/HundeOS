#ifndef __KERNEL_SUBSYSTEMS_IO_CONSOLE_CONSOLE_H
#define __KERNEL_SUBSYSTEMS_IO_CONSOLE_CONSOLE_H
#include "../io.h"

typedef struct ConsoleState
{
    IOState state; // must be the first member as we rely on this for inheritance
    Buffer console_buffer;
} ConsoleState;

typedef ConsoleState * ConsoleStatePtr;

ConsoleStatePtr ConsoleState__heap_new();
IOPtr Console__heap_new();

#endif
