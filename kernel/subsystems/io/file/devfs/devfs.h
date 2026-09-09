#ifndef __KERNEL_SUBSYSTEMS_IO_FILE_DEVFS_DEVFS_H
#define __KERNEL_SUBSYSTEMS_IO_FILE_DEVFS_DEVFS_H

#include "../filesystem.h"

typedef struct IOSubsystem IOSubsystem;
typedef IOSubsystem * IOSubsystemPtr;

typedef struct DevFilesystemIOState
{
    FilesystemIOState state;
    IOSubsystemPtr io_subsystem;
    IOInternalHandle next_handle;
} DevFilesystemIOState;

typedef DevFilesystemIOState * DevFilesystemIOStatePtr;

FileIO DevFilesystemIO__new();
DevFilesystemIOStatePtr DevFilesystemIOState__heap_new(FilesystemPtr fs, IOSubsystemPtr io_subsystem);

#endif
