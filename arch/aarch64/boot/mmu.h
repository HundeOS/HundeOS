#ifndef __AARCH64_BOOT_MMU_H_
#define __AARCH64_BOOT_MMU_H_

#include "platform.h"

void setup_boot_mappings(BootPlatformInfoPtr platform, SystemInfoPtr system_info);
void enable_mmu();

#endif
