#ifndef HUND_SYSINFOFS_H
#define HUND_SYSINFOFS_H
#include "hardware/boot_info.h"
#include "io/filesystem_driver.h"
Result(FilesystemFileSize) Sysinfofs__render(BootHardwareInfoPtr info, FilesystemFileDataPtr data);
FilesystemOperations Sysinfofs__operations(void);
#endif
