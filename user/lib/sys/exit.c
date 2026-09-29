#include <stdio.h>

#include "syscall.h"
#include "trace/io_trace.h"

void exit(int code)
{
    fflush(stdout);
    fflush(stderr);
    IOTrace__drain();
    syscall(PROC_EXIT, code, 0, 0, 0, 0);
}
