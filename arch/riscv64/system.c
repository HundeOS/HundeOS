#include "../../kernel/arch/system.h"

#include "boot/common/platform.h"

void System__shutdown(void)
{
    SBI__shutdown();
}
