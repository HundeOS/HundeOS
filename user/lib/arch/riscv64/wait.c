#include "sys/interrupt.h"
#include "sys/syscall.h"

void wait_for_interrupt(void)
{
    syscall(SYSCALL_PROCESS_IDLE_WAIT, 0, 0, 0, 0, 0);
}
