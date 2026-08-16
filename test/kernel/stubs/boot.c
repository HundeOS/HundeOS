#include "../../../kernel/boot.h"
#include "../../../kernel/kernel_core.h"
#include "../../../kernel/kernel_init.h"

void Boot__init_bootstrap_system(SystemInfoPtr system_info)
{
    KernelPtr kernel = Kernel__instance();
    kernel->system_info = Some(SystemInfo, *system_info);
    SystemInfoPtr installed_system_info = &kernel->system_info.some.val;
    kernel->scheduler->interrupt_controller = Some(InterruptControllerInfo, installed_system_info->interrupt_controller);
    kernel->irq_dispatcher->interrupt_controller = Some(InterruptControllerInfo, installed_system_info->interrupt_controller);
}
