#include "sys/wait.h"

void wait_for_interrupt(void)
{
    __asm__ __volatile__("wfi" ::: "memory");
}
