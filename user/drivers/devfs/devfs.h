#ifndef HUND_USER_DEVFS_H
#define HUND_USER_DEVFS_H
#include "devices/device_registry.h"
#include "io/filesystem_driver.h"

FilesystemOperations Devfs__operations(void);
#endif
