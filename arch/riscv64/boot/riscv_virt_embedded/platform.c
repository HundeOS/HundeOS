#include "../common/platform.h"

#define RISCV_BOOT_TEXT __attribute__((section(".boot.text")))

RISCV_BOOT_TEXT void RiscVBootPlatform__prepare_hardware(void)
{
}

void RiscVBootPlatform__configure_system_info(SystemInfoPtr info)
{
    info->default_user_stack_size = 2ULL * 1024ULL * 1024ULL;
    info->console_uart = info->trace_uart;
}
