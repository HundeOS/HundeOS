#include <stdbool.h>

#include "../../../kernel/arch/system.h"

static bool shutdown_called = false;

void System__shutdown()
{
    shutdown_called = true;
}

void reset_system_state(void)
{
    shutdown_called = false;
}

bool system_shutdown_called(void)
{
    return shutdown_called;
}
