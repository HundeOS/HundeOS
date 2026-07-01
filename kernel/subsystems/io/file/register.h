#ifndef __KERNEL_SUBSYSTEMS_IO_FILE_REGISTER_H
#define __KERNEL_SUBSYSTEMS_IO_FILE_REGISTER_H
#include "filesystem.h"

FilesystemPtr Filesystem__build_root_and_mounts();
void FilesystemTable__mount_available_entries(FilesystemPtr root_fs);

#endif
