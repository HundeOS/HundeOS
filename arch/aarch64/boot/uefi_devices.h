#ifndef __AARCH64_BOOT_UEFI_DEVICES_H_
#define __AARCH64_BOOT_UEFI_DEVICES_H_

#include "boot.h"
#include "platform.h"

__BOOT__(void) UefiDevices__configure(BootPlatformInfoPtr self);

#endif
