#include "boot.h"

#include "arch/init.h"
#include "artwork.h"
#include "kernel_core.h"
#include "kernel_init.h"

void Boot__init_bootstrap_system(SystemInfoPtr system_info)
{
    KernelPtr kernel = Kernel__instance();
    kernel->system_info = Some(SystemInfo, *system_info);
    Arch__init_bootstrap_system(system_info);
    SystemArtwork__show_launch(system_info);
}
