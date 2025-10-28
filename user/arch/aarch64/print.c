#include <stdint.h>

#include "syscall.h"

void print(const char * str) {
    syscall(PRINT, (uint64_t)str, 0, 0, 0, 0);
}