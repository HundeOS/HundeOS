#ifndef __KERNEL_SUBSYSTEMS_IO_FILE_REGISTER_H
#define __KERNEL_SUBSYSTEMS_IO_FILE_REGISTER_H
#include "filesystem.h"
#include "syscalls/io.h"

typedef struct Kernel Kernel;
typedef Kernel * KernelPtr;

FilesystemPtr Filesystem__build_root_and_mounts(KernelPtr kernel);
Result(IOStatus) Filesystem__mount_block_device(
    FilesystemPtr root_fs, ConstBStringPtr device_path, ConstBStringPtr filesystem_type, ConstBStringPtr mountpoint
);
void FilesystemTable__mount_available_entries(FilesystemPtr root_fs);

#endif
