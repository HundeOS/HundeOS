#ifndef __AARCH64_BOOT_UEFI_H_
#define __AARCH64_BOOT_UEFI_H_

#include "boot.h"
#include "platform.h"

__BOOT__(void) UefiAcpi__apply_system_table(BootPlatformInfoPtr platform, uint64_t system_table);

#endif
