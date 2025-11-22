#ifndef __KERNEL_SUBSYSTEMS_IO_EMBEDDED_EMBEDDED_H
#define __KERNEL_SUBSYSTEMS_IO_EMBEDDED_EMBEDDED_H

#include <stdint.h>
#include <stdint.h>
#include <stdint.h>
#include "../filesystem.h"
#include "../../../../types/comparison.h"

typedef uint64_t HandleCount;

typedef struct EmbeddedFilesystemIOState
{
    // inheritance, leave as first item
    FilesystemIOState state;
    HandleCount handle_count;
}
EmbeddedFilesystemIOState;

typedef EmbeddedFilesystemIOState * EmbeddedFilesystemIOStatePtr;

FileIO EmbeddedFilesystemIO__new();
EmbeddedFilesystemIOStatePtr EmbeddedFilesystemIOState__heap_new(FilesystemPtr fs);

#endif