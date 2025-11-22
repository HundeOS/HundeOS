#ifndef __KERNEL_SUBSYSTEMS_IO_FILE_FILESYSTEM_IMPL_H
#define __KERNEL_SUBSYSTEMS_IO_FILE_FILESYSTEM_IMPL_H

#include "../io.h"
#include "../types.h"
#include "filesystem_node.h"

typedef Result(FilesystemNodes) (* FS_list_nodes)(IOStatePtr state, FilesystemNodePtr parent, KString path);

typedef struct FileIO
{
    // inheritance, leave as first item
    IO base_io;
    FS_list_nodes list_nodes;
}
FileIO;

typedef FileIO * FileIOPtr;

#endif