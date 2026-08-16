#ifndef __KERNEL_SUBSYSTEMS_IO_FILE_REGISTER_H
#define __KERNEL_SUBSYSTEMS_IO_FILE_REGISTER_H
#include "filesystem.h"

typedef struct Kernel Kernel;
typedef Kernel * KernelPtr;

FilesystemPtr Filesystem__build_root_and_mounts(KernelPtr kernel);
void FilesystemTable__mount_available_entries(FilesystemPtr root_fs);

#endif
