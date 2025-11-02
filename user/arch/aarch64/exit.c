#include "syscall.h"

void exit(int code)
{
    syscall(PROC_EXIT, code, 0, 0, 0, 0);
}

