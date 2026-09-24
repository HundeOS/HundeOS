#include "../../../kernel/boot.h"
#include "../../../kernel/kernel_core.h"
#include "../../../kernel/kernel_init.h"

void Boot__init_bootstrap_system(SystemInfoPtr system_info)
{
    KernelPtr kernel = Kernel__instance();
    kernel->system_info = Some(SystemInfo, *system_info);
}
