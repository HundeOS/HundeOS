#ifndef __KERNEL_SUBSYSTEMS_IO_FILE_NODE_H
#define __KERNEL_SUBSYSTEMS_IO_FILE_NODE_H

#include "../../../types/optional.h"
#include "../../../types/result.h"
#include "../../../types/fixed_array.h"
#include "../../../types/bstring.h"
#include "../../../all_defs.h"
#include "../../../../common_includes/syscalls/process.h"
#include "../../../../common_includes/syscalls/io.h"

typedef uint64_t FilesystemAttributes;
typedef uint16_t FilesystemPermissions;

typedef struct FilesystemNode FilesystemNode;
typedef FilesystemNode * FilesystemNodePtr;

typedef struct Filesystem Filesystem;
typedef Filesystem * FilesystemPtr;

OptionalDef(FilesystemNodePtr);
ResultDef(FilesystemNodePtr);

typedef struct FilesystemNode
{
    FilesystemAttributes attribs;
    UserID user_id;
    GroupID group_id;
    FilesystemPermissions mode;
    BString name;

    Optional(FilesystemNodePtr) parent;
    FilesystemPtr filesystem;
} FilesystemNode;

OptionalDef(FilesystemNode);
ResultDef(FilesystemNode);

FilesystemNodePtr FilesystemNode__heap_new(FilesystemPtr filesystem, ConstBStringPtr name,
                                           FilesystemAttributes attribs);
FilesystemNodePtr FilesystemNode__heap_new_with_metadata(FilesystemPtr filesystem, ConstBStringPtr name,
                                                         FilesystemAttributes attribs, UserID user_id, GroupID group_id,
                                                         FilesystemPermissions mode);

FixedArrayDefine(FilesystemNodePtr, MAX_CHILD_NODES, FilesystemNodes, Ptr__equals);
typedef FilesystemNodes * FilesystemNodesPtr;
typedef const FilesystemNodes * ConstFilesystemNodesPtr;
ResultDef(FilesystemNodes);

// Result(FilesystemNodes) FilesystemNode__list(FilesystemNodePtr self, BString path);

#endif
