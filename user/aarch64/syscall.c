#include <stdint.h>

uint32_t syscall(uint32_t num, uint32_t arg1, uint32_t arg2, uint32_t arg3) {
    uint32_t result;

    register uint64_t x0 asm("x0") = arg1;
    register uint64_t x1 asm("x1") = arg2;
    register uint64_t x2 asm("x2") = arg3;
    register uint64_t x8 asm("x8") = num;

    asm volatile (
        "svc #0\n"
                  : "+r"(x0)
                  : "r"(x1), "r"(x2), "r"(x8)
                  : "memory", "cc");
    return x0; // syscall result
}