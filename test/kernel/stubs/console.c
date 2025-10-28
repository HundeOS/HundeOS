#include "../../../kernel/subsystems/io/console/console.h"

ConsoleState console_state = (ConsoleState) {
    .state = NEW_IO_STATE,
    .write_buffer = EmptyRingBuffer(IOBuffer),
    .read_buffer = EmptyRingBuffer(IOBuffer),
};
