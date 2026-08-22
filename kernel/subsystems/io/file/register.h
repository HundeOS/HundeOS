#ifndef __KERNEL_SUBSYSTEMS_IO_FILE_REGISTER_H
#define __KERNEL_SUBSYSTEMS_IO_FILE_REGISTER_H
#include "filesystem.h"
#include "syscalls/io.h"

typedef struct Kernel Kernel;
typedef Kernel * KernelPtr;

FilesystemPtr Filesystem__build_root_and_mounts(KernelPtr kernel);
Result(IOStatus) Filesystem__mount_block_device(
    FilesystemPtr root_fs, BString device_path, BString filesystem_type, BString mountpoint
);
void FilesystemTable__mount_available_entries(FilesystemPtr root_fs);

#endif
