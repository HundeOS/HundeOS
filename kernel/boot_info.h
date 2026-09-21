#ifndef HUND_KERNEL_BOOT_INFO_H
#define HUND_KERNEL_BOOT_INFO_H
#include "hardware/boot_info.h"
#include "system_info.h"
BootHardwareInfo BootHardwareInfo__from_system(SystemInfoPtr info, uint64_t online_cpu_count);
#endif
