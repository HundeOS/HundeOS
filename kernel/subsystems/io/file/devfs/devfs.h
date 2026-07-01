#ifndef __KERNEL_SUBSYSTEMS_IO_FILE_DEVFS_DEVFS_H
#define __KERNEL_SUBSYSTEMS_IO_FILE_DEVFS_DEVFS_H

#include "../filesystem.h"

typedef struct DevFilesystemIOState
{
    FilesystemIOState state;
}
DevFilesystemIOState;

typedef DevFilesystemIOState * DevFilesystemIOStatePtr;

FileIO DevFilesystemIO__new();
DevFilesystemIOStatePtr DevFilesystemIOState__heap_new(FilesystemPtr fs);

#endif
