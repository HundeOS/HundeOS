#include <stdlib.h>
#include "unity.h"
#include "../../../kernel/arch/process.h"
#include <stdbool.h>

static bool panic_state = false;

void Kernel__panic(const char * message, ProcessContext * ctx)
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