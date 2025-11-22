#ifndef __KERNEL_SUBSYSTEMS_IO_FILE_NODE_H
#define __KERNEL_SUBSYSTEMS_IO_FILE_NODE_H

#include "../../../types/optional.h"
#include "../../../types/result.h"
#include "../../../types/fixed_array.h"
#include "../../../types/kstring.h"

typedef uint64_t FilesystemAttributes;

typedef struct FilesystemNode FilesystemNode;
typedef FilesystemNode * FilesystemNodePtr;

typedef struct Filesystem Filesystem;
typedef Filesystem * FilesystemPtr;

OptionalDef(FilesystemNodePtr);
ResultDef(FilesystemNodePtr);

typedef struct FilesystemNode
{
    FilesystemAttributes attribs;
    KString name;

    Optional(FilesystemNodePtr) parent;
    FilesystemPtr filesystem;
}
FilesystemNode;

OptionalDef(FilesystemNode);
ResultDef(FilesystemNode);

FilesystemNodePtr FilesystemNode__heap_new(FilesystemPtr filesystem, KString name, FilesystemAttributes attribs);

FixedArrayDefine(FilesystemNodePtr, MAX_CHILD_NODES, FilesystemNodes, Ptr__equals);
ResultDef(FilesystemNodes);

// Result(FilesystemNodes) FilesystemNode__list(FilesystemNodePtr self, KString path);

#endif