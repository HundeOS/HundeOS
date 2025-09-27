#include <stdint.h>

uint64_t syscall(uint64_t num, uint64_t arg1, uint64_t arg2, uint64_t arg3) {
    uint64_t result;

    register uint64_t x0 __asm__("x0") = arg1;
    register uint64_t x1 __asm__("x1") = arg2;
    register uint64_t x2 __asm__("x2") = arg3;
    register uint64_t x8 __asm__("x8") = num;

    __asm__ __volatile__ (
        "svc #0\n"
                  : "+r"(x0)
                  : "r"(x1), "r"(x2), "r"(x8)
                  : "memory", "cc");
    return x0; // syscall result
}