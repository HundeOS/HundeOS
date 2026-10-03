#include "sys/interrupt.h"

void wait_for_interrupt()
{
    __asm__ __volatile__("wfi");
}
