#ifndef __KERNEL_SUBSYSTEMS_IO_FILE_PROCFS_PROCFS_H
#define __KERNEL_SUBSYSTEMS_IO_FILE_PROCFS_PROCFS_H

#include "../filesystem.h"

typedef struct Kernel Kernel;
typedef Kernel * KernelPtr;

typedef struct ProcFilesystemIOState
{
    FilesystemIOState state;
    KernelPtr kernel;
    IOInternalHandle next_handle;
}
ProcFilesystemIOState;

typedef ProcFilesystemIOState * ProcFilesystemIOStatePtr;

FileIO ProcFilesystemIO__new();
ProcFilesystemIOStatePtr ProcFilesystemIOState__heap_new(FilesystemPtr fs, KernelPtr kernel);

#endif
