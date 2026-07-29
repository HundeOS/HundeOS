#include "../../sys/syscall.h"


SystemCallResult syscall(
    SystemCall num, Anything arg1, Anything arg2, Anything arg3, Anything arg4, Anything arg5
)
{
    register Anything x0 __asm__("x0") = arg1;
    register Anything x1 __asm__("x1") = arg2;
    register Anything x2 __asm__("x2") = arg3;
    register Anything x3 __asm__("x3") = arg4;
    register Anything x4 __asm__("x4") = arg5;
    register Anything x8 __asm__("x8") = num;

    __asm__ __volatile__ (
        "svc #0\n"
                  : "+r"(x0)
                  : "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x8)
                  : "memory", "cc");
    return x0; // syscall result
}
