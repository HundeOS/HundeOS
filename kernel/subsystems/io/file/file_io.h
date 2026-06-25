#ifndef __KERNEL_SUBSYSTEMS_IO_FILE_FILESYSTEM_IMPL_H
#define __KERNEL_SUBSYSTEMS_IO_FILE_FILESYSTEM_IMPL_H

#include "../io.h"
#include "../types.h"
#include "filesystem_node.h"

#ifndef __KERNEL_IO_STATUS_RESULT_DEFINED
#define __KERNEL_IO_STATUS_RESULT_DEFINED
ResultDef(IOStatus);
#endif

typedef Result(FilesystemNodes) (* FS_list_nodes)(IOStatePtr self, FilesystemNodePtr parent, KString path);
typedef Result(IOStatus) (* FS_mkdir)(IOStatePtr self, KString path);
typedef Result(IOStatus) (* FS_remove)(IOStatePtr self, KString path, Flags flags);

typedef struct FileIO
{
    // inheritance, leave as first item
    IO base_io;
    FS_list_nodes list_nodes;
    FS_mkdir mkdir;
    FS_remove remove;
}
FileIO;

typedef FileIO * FileIOPtr;

#endif
