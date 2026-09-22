#include "sys/syscall.h"

SystemCallResult syscall(SystemCall num, Anything arg1, Anything arg2, Anything arg3, Anything arg4, Anything arg5)
{
    register Anything a0 __asm__("a0") = arg1;
    register Anything a1 __asm__("a1") = arg2;
    register Anything a2 __asm__("a2") = arg3;
    register Anything a3 __asm__("a3") = arg4;
    register Anything a4 __asm__("a4") = arg5;
    register Anything a7 __asm__("a7") = num;

    __asm__ __volatile__("ecall" : "+r"(a0) : "r"(a1), "r"(a2), "r"(a3), "r"(a4), "r"(a7) : "memory");
    return a0;
}
