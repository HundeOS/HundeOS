#include <stdbool.h>
#include "idle_debug.h"
#include "sys/interrupt.h"
#include "../../user.h"

int main(void)
{
    IdleDebugState volatile * state = (IdleDebugStatePtr)IDLE_DEBUG_PAGE_VADDR;
    state->magic = IDLE_DEBUG_MAGIC;
    while (true)
    {
        state->before_wfi_count++;
        wait_for_interrupt();
        state->after_wfi_count++;
    }
}
