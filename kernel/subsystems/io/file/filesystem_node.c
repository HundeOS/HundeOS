#include "filesystem_node.h"
#include "filesystem.h"

FixedArrayBodyDefine(FilesystemNodes);

FilesystemNodePtr FilesystemNode__heap_new(FilesystemPtr filesystem, KString name, FilesystemAttributes attribs)
{
    FilesystemNodePtr result = ptr(Kernel__alloc_kernel(sizeof(FilesystemNode)), FilesystemNode);
    *result = (FilesystemNode) {
        .attribs = attribs,
        .name = name,
        .filesystem = filesystem,
        .parent = None(FilesystemNodePtr)
    };
    return result;
}