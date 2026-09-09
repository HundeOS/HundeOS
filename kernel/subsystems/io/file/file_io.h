#ifndef __KERNEL_SUBSYSTEMS_IO_FILE_FILESYSTEM_IMPL_H
#define __KERNEL_SUBSYSTEMS_IO_FILE_FILESYSTEM_IMPL_H

#include "../io.h"
#include "../types.h"
#include "filesystem_node.h"

#ifndef __KERNEL_IO_STATUS_RESULT_DEFINED
#define __KERNEL_IO_STATUS_RESULT_DEFINED
ResultDef(IOStatus);
#endif

#ifndef __KERNEL_DIRECTORY_ENTRY_COUNT_RESULT_DEFINED
#define __KERNEL_DIRECTORY_ENTRY_COUNT_RESULT_DEFINED
ResultDef(DirectoryEntryCount);
#endif

typedef Result(FilesystemNodes) (*FS_list_nodes)(IOStatePtr self, FilesystemNodePtr parent, ConstBStringPtr path);
typedef Result(DirectoryEntryCount) (*FS_list_dir)(IOStatePtr self, ConstBStringPtr path, DirectoryEntryPtr entries,
                                                   size_t max_entries);
typedef Result(IOStatus) (*FS_mkdir)(IOStatePtr self, ConstBStringPtr path);
typedef Result(IOStatus) (*FS_remove)(IOStatePtr self, ConstBStringPtr path, Flags flags);
typedef Result(IOStatus) (*FS_link)(IOStatePtr self, ConstBStringPtr source_path, ConstBStringPtr target_path);

typedef struct FileIO
{
    // inheritance, leave as first item
    IO base_io;
    FS_list_nodes list_nodes;
    FS_list_dir list_dir;
    FS_mkdir mkdir;
    FS_remove remove;
    FS_link link;
} FileIO;

typedef FileIO * FileIOPtr;

Result(FilesystemNodes) FileIO__unsupported_list_nodes(IOStatePtr self, FilesystemNodePtr parent, ConstBStringPtr path);
Result(DirectoryEntryCount)
    FileIO__unsupported_list_dir(IOStatePtr self, ConstBStringPtr path, DirectoryEntryPtr entries, size_t max_entries);
Result(IOStatus) FileIO__unsupported_mkdir(IOStatePtr self, ConstBStringPtr path);
Result(IOStatus) FileIO__unsupported_remove(IOStatePtr self, ConstBStringPtr path, Flags flags);
Result(IOStatus) FileIO__unsupported_link(IOStatePtr self, ConstBStringPtr source_path, ConstBStringPtr target_path);

#endif
