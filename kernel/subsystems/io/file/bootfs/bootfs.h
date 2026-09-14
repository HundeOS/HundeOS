#ifndef __KERNEL_SUBSYSTEMS_IO_FILE_BOOTFS_BOOTFS_H
#define __KERNEL_SUBSYSTEMS_IO_FILE_BOOTFS_BOOTFS_H

#include "../filesystem.h"

typedef struct Kernel Kernel;
typedef Kernel * KernelPtr;

typedef struct BootFilesystemIOState
{
    FilesystemIOState state;
    KernelPtr kernel;
    IOInternalHandle next_handle;
} BootFilesystemIOState;

typedef BootFilesystemIOState * BootFilesystemIOStatePtr;

FileIO BootFilesystemIO__new();
BootFilesystemIOStatePtr BootFilesystemIOState__heap_new(FilesystemPtr fs, KernelPtr kernel);

#endif
