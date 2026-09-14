#ifndef HUND_USER_PROCFS_H
#define HUND_USER_PROCFS_H
#include "process/process_registry.h"
#include "io/filesystem_driver.h"
FilesystemOperations Procfs__operations(void);
#endif
