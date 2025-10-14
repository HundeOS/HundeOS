#include "syscall_handler.h"
#include <stdint.h>

#include "debug.h"
#include "print.h"



int SyscallHandler__handle(int syscall_id, Anything arg1, Anything arg2, Anything arg3)
{
    __d_printf(DEBUG, "SyscallHandler__handle(syscall_id=%i, arg1=%i, arg2=%i, arg3=%i)", syscall_id, arg1, arg2, arg3);
    if (syscall_id == 0x21) {
        k_println(str(arg1));
    }
    if (syscall_id == 0xFF) {
        k_println("Ping!");
    }
    if (syscall_id == 0x00) {
        k_println("Process exit");
    }
    return 0;
}