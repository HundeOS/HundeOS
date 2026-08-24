#include "embedded.h"

#include "../file_io.h"
#include "../../../../bin/embedded_binaries.h"
#include "../../../../utils/math.h"
#include "../../../../kalloc.h"

static BString invalid_path_format = BSTRING_NEW("Invalid path %s");
static BString invalid_file_handle = BSTRING_NEW("Invalid file handle");
static BString not_implemented = BSTRING_NEW("Operation not implemented");
static BString end_of_file = BSTRING_NEW("End pf file");
static const BString drivers_name = BSTRING_NEW("drivers");
static const BString drivers_prefix = BSTRING_NEW("drivers/");
static const BString services_name = BSTRING_NEW("services");
static const BString services_prefix = BSTRING_NEW("services/");

static BString EmbeddedFileIO__relative_path(BString path)
{
    StringPos start = 0;
    StringPos end = BString__length(&path);

    while (start < end)
    {
        char ch = BString__char_at_or(&path, start, '\0');
        if (ch != '/')
        {
            break;
        }
        start++;
    }

    while (end > start)
    {
        char ch = BString__char_at_or(&path, end - 1, '\0');
        if (ch != '/')
        {
            break;
        }
        end--;
    }

    return BString__slice(&path, start, end);
}

static bool EmbeddedFileIO__contains_node(FilesystemNodes nodes, BString name)
{
    for_each_fa(FilesystemNodes, nodes, index, node,
    {
        if (BString__equals(&node->name, &name))
        {
            return true;
        }

    });
    return false;
}

static void EmbeddedFileIO__metadata_for_child(
    BString parent_path,
    BString child_name,
    bool is_directory,
    UserID * user_id,
    GroupID * group_id,
    FilesystemPermissions * mode
)
{
    bool is_hund_path = false;

    if (BString__length(&parent_path) == 0)
    {
        is_hund_path = is_directory
            && (BString__equals(&child_name, &drivers_name) || BString__equals(&child_name, &services_name));
    }
    else if (BString__equals(&parent_path, &drivers_name) || BString__starts_with(&parent_path, &drivers_prefix))
    {
        is_hund_path = true;
    }
    else if (BString__equals(&parent_path, &services_name) || BString__starts_with(&parent_path, &services_prefix))
    {
        is_hund_path = true;
    }

    if (is_hund_path)
    {
        *user_id = PROCESS_USER_HUND;
        *group_id = PROCESS_GROUP_HUND;
        *mode = is_directory
            ? (IO_MODE_TYPE_DIR | IO_MODE_OWNER_READ | IO_MODE_OWNER_EXEC | IO_MODE_GROUP_READ | IO_MODE_GROUP_EXEC)
            : (IO_MODE_TYPE_FILE | IO_MODE_OWNER_READ | IO_MODE_OWNER_EXEC | IO_MODE_GROUP_READ | IO_MODE_GROUP_EXEC);
        return;
    }

    *user_id = PROCESS_USER_ROOT;
    *group_id = PROCESS_GROUP_ROOT;
    *mode = is_directory
        ? (IO_MODE_TYPE_DIR
            | IO_MODE_OWNER_READ | IO_MODE_OWNER_EXEC
            | IO_MODE_GROUP_READ | IO_MODE_GROUP_EXEC
            | IO_MODE_OTHER_READ | IO_MODE_OTHER_EXEC)
        : (IO_MODE_TYPE_FILE
            | IO_MODE_OWNER_READ | IO_MODE_OWNER_EXEC
            | IO_MODE_GROUP_READ | IO_MODE_GROUP_EXEC
            | IO_MODE_OTHER_READ | IO_MODE_OTHER_EXEC);
}

static Optional(FilesystemNodePtr) EmbeddedFileIO__node_for_entry(
    FilesystemPtr filesystem,
    BString parent_path,
    BString entry_name
)
{
    BString relative_parent = EmbeddedFileIO__relative_path(parent_path);
    BString relative_entry = EmbeddedFileIO__relative_path(entry_name);

    if (BString__length(&relative_entry) == 0)
    {
        return None(FilesystemNodePtr);
    }

    BString remaining = relative_entry;
    if (BString__length(&relative_parent) > 0)
    {
        if (!BString__starts_with(&relative_entry, &relative_parent))
        {
            return None(FilesystemNodePtr);
        }

        if (BString__length(&relative_entry) == BString__length(&relative_parent))
        {
            return None(FilesystemNodePtr);
        }

        char separator = BString__char_at_or(&relative_entry, BString__length(&relative_parent), '\0');
        if (separator != '/')
        {
            return None(FilesystemNodePtr);
        }

        remaining = BString__slice(
            &relative_entry,
            BString__length(&relative_parent) + 1,
            BString__length(&relative_entry)
        );
    }

    BStringArray segments = BString__split(&remaining, '/');
    if (BStringArray__len(&segments) == 0)
    {
        return None(FilesystemNodePtr);
    }

    BString child_name = EMPTY_BSTRING;
    match_optional(BString, BStringArray__at(&segments, 0),
        some(segment) { child_name = segment; },
        none() { return None(FilesystemNodePtr); }
    );
    bool is_directory = BStringArray__len(&segments) > 1;
    UserID user_id = PROCESS_USER_ROOT;
    GroupID group_id = PROCESS_GROUP_ROOT;
    FilesystemPermissions mode = 0;
    EmbeddedFileIO__metadata_for_child(relative_parent, child_name, is_directory, &user_id, &group_id, &mode);

    return Some(
        FilesystemNodePtr,
        FilesystemNode__heap_new_with_metadata(
            filesystem,
            child_name,
            IO_DIRENT_ATTR_NONE,
            user_id,
            group_id,
            mode
        )
    );
}

Result(IOInternalHandle) EmbeddedFileIO__open(IOStatePtr self, BString path, Flags flags)
{
    EmbeddedFilesystemIOStatePtr fs_state = (EmbeddedFilesystemIOStatePtr) self;
    BString relative_path = EmbeddedFileIO__relative_path(path);

    if (BString__length(&relative_path) == 0)
    {
        return Error(IOInternalHandle, BString__sprintf(&invalid_path_format, cstr(path)), IO_INVALID_PATH);
    }

    match_optional(Elf, EmbeddedElf__load(relative_path),
        some(elf)
        {
            critical_section(fs_state->state.lock,
            {
                IOInternalHandle handle = (IOInternalHandle) fs_state->handle_count++ | fs_state->state.filesystem->fs_code;
                FileHandleTargetMap__set(&fs_state->state.handles, handle, FileHandleTarget__heap_new(relative_path));
                cs_return Success(IOInternalHandle, handle);
            });
        },
        none_do_nothing()
    );

    return Error(IOInternalHandle, BString__sprintf(&invalid_path_format, cstr(path)), IO_INVALID_PATH);
}

Result(IOInternalHandle) EmbeddedFileIO__close(IOStatePtr self, IOInternalHandle handle)
{
    return Error(IOInternalHandle, not_implemented, IO_OPERATION_NOT_IMPLEMENTED);
}

Result(IOBufferPtr) EmbeddedFileIO__read(
    IOStatePtr self, IOInternalHandle handle, size_t size, ExternalBuffer target_buffer
)
{
    EmbeddedFilesystemIOStatePtr fs_state = (EmbeddedFilesystemIOStatePtr) self;

    Optional(FileHandleTargetPtr) target;

    critical_section(fs_state->state.lock,
    {
        target = FileHandleTargetMap__get(&fs_state->state.handles, handle);
    });
    match_optional(FileHandleTargetPtr, target,
        some(target_ptr)
        {
            if (IOBuffer__item_count(&target_ptr->buffer) < size)
            {
                size_t free_space = IOBuffer__free_space(&target_ptr->buffer);

                match_optional(Elf, EmbeddedElf__load(target_ptr->path),
                    some(elf)
                    {
                        CharPtr bytes = (CharPtr) elf.start;
                        size_t elf_size = elf.end - elf.start;

                        if (target_ptr->offset >= elf_size)
                        {
                            return Error(IOBufferPtr, end_of_file, IO_EOF);
                        }

                        size_t remaining = elf_size - target_ptr->offset;
                        size_t read_size = min(size, min(remaining, free_space));

                        IOBuffer__push_all(&target_ptr->buffer, &bytes[target_ptr->offset], read_size);
                        target_ptr->offset += read_size;
                    },
                    none()
                    {
                        return Error(IOBufferPtr,
                                     BString__sprintf(&invalid_path_format, cstr(target_ptr->path)),
                                     IO_INVALID_PATH);
                    }
                );

            }

            return Success(IOBufferPtr, &target_ptr->buffer);
        },
        none()
        {
            return Error(IOBufferPtr, invalid_file_handle, IO_UNKNOWN_HANDLE);
        }
    );

}

Result(ByteCount) EmbeddedFileIO__write(IOStatePtr self, IOInternalHandle handle, DataPtr data, size_t size)
{
    return Error(ByteCount, not_implemented, IO_OPERATION_NOT_IMPLEMENTED);
}

Result(ParkStatus) EmbeddedFileIO__park(IOStatePtr self, IOContextPtr parked_context)
{
    return Error(ParkStatus, not_implemented, IO_OPERATION_NOT_IMPLEMENTED);
}

Result(ParkStatus) EmbeddedFileIO__unpark(IOStatePtr self)
{
    return Error(ParkStatus, not_implemented, IO_OPERATION_NOT_IMPLEMENTED);
}

Result(FilesystemNodes) EmbeddedFileIO__list_nodes(IOStatePtr self, FilesystemNodePtr parent, BString path)
{
    EmbeddedFilesystemIOStatePtr fs_state = (EmbeddedFilesystemIOStatePtr) self;
    FilesystemNodes result = EmptyArray(FilesystemNodes);
    BString relative_path = EmbeddedFileIO__relative_path(path);
    for_each_fa(BStringArray, EmbeddedElf__list(), index, entry,
    {
        match_optional(FilesystemNodePtr, EmbeddedFileIO__node_for_entry(fs_state->state.filesystem, relative_path, entry),
            some(node)
            {
                if (!EmbeddedFileIO__contains_node(result, node->name))
                {
                    FilesystemNodes__push(&result, node);
                }
            },
            none_do_nothing()
        );

    });
    return Success(FilesystemNodes, result);
}

Result(IOStatus) EmbeddedFileIO__mkdir(IOStatePtr self, BString path)
{
    return Error(IOStatus, not_implemented, IO_OPERATION_NOT_SUPPORTED);
}

Result(IOStatus) EmbeddedFileIO__remove(IOStatePtr self, BString path, Flags flags)
{
    return Error(IOStatus, not_implemented, IO_OPERATION_NOT_SUPPORTED);
}

Result(IOStatus) EmbeddedFileIO__link(IOStatePtr self, BString source_path, BString target_path)
{
    return Error(IOStatus, not_implemented, IO_OPERATION_NOT_SUPPORTED);
}

FileIO EmbeddedFilesystemIO__new()
{
    FileIO embedded_file_io = (FileIO) {
        .base_io = (IO) {
            .read = EmbeddedFileIO__read,
            .write = EmbeddedFileIO__write,
            .open = EmbeddedFileIO__open,
            .close = EmbeddedFileIO__close,
            .park = EmbeddedFileIO__park,
            .unpark = EmbeddedFileIO__unpark,
        },
        .list_nodes = EmbeddedFileIO__list_nodes,
        .mkdir = EmbeddedFileIO__mkdir,
        .remove = EmbeddedFileIO__remove,
        .link = EmbeddedFileIO__link,
    };
    return embedded_file_io;
}

EmbeddedFilesystemIOStatePtr EmbeddedFilesystemIOState__heap_new(FilesystemPtr fs)
{
    EmbeddedFilesystemIOStatePtr io_state = ptr(
            Kernel__alloc_kernel(sizeof(EmbeddedFilesystemIOState)), EmbeddedFilesystemIOState
        );

    *io_state = (EmbeddedFilesystemIOState) {
        .state = FilesystemIOState__new(fs),
        .handle_count = 0,
    };

    fs->state = &io_state->state;

    return io_state;
}
