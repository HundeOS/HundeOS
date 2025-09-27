#include <stdint.h>

#include "syscall.h"

void dummy() {
    const char * message = "Hello World!";
    syscall(0x21, (uint64_t)message, 0, 0);
}