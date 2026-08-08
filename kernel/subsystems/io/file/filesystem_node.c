#include "filesystem_node.h"
#include "filesystem.h"
#include "../../../kalloc.h"

FixedArrayBodyDefine(FilesystemNodes);

FilesystemNodePtr FilesystemNode__heap_new(FilesystemPtr filesystem, BString name, FilesystemAttributes attribs)
{
    return FilesystemNode__heap_new_with_metadata(
        filesystem,
        name,
        attribs,
        PROCESS_USER_ROOT,
        PROCESS_GROUP_ROOT,
        IO_MODE_OWNER_READ | IO_MODE_OWNER_EXEC
            | IO_MODE_GROUP_READ | IO_MODE_GROUP_EXEC
            | IO_MODE_OTHER_READ | IO_MODE_OTHER_EXEC
    );
}

FilesystemNodePtr FilesystemNode__heap_new_with_metadata(
    FilesystemPtr filesystem,
    BString name,
    FilesystemAttributes attribs,
    UserID user_id,
    GroupID group_id,
    FilesystemPermissions mode
)
{
    FilesystemNodePtr result = ptr(Kernel__alloc_kernel(sizeof(FilesystemNode)), FilesystemNode);
    *result = (FilesystemNode) {
        .attribs = attribs,
        .user_id = user_id,
        .group_id = group_id,
        .mode = mode,
        .name = name,
        .filesystem = filesystem,
        .parent = None(FilesystemNodePtr)
    };
    return result;
}
