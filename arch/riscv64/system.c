#include "../../kernel/arch/system.h"

#include "boot/qemu_virt/platform.h"

void System__shutdown(void)
{
    SBI__shutdown();
}
