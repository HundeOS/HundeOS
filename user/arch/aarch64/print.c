#include <stdint.h>

#include "syscall.h"

void println(const char * str) {
    syscall(CONSOLE_PRINT, (uint64_t)str, 0, 0);
}