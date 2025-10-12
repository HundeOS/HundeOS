#include <stdlib.h>
#include <stdbool.h>
#include "unity.h"
#include "../../../kernel/arch/panic.h"

static bool panic_state = false;

void Kernel__panic(const char * message, exception_context_t * ctx)
{
    panic_state = true;
    printf(message);
}

bool panic_occurred()
{
    return panic_state;
}

void clear_panic_state()
{
    panic_state = false;
}