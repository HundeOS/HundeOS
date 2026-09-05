#include "devfs.h"

#include "../../../../kalloc.h"
#include "../../io_subsystem.h"
#include "../../block/block_device.h"

static BString invalid_path = bstr("Invalid devfs path");
static BString invalid_handle = bstr("Invalid devfs handle");
static BString metadata_format = bstr("path=%B\nblock_size=%i\nblock_count=%i\nuserspace_service=%i\n");

BlockDevicePaths BlockDeviceRegistry__paths() __attribute__((weak));
BlockDevicePaths BlockDeviceRegistry__paths()
{
    return EmptyArray(BlockDevicePaths);
}

BlockDeviceMetadataEntries BlockDeviceRegistry__metadata_entries() __attribute__((weak));
BlockDeviceMetadataEntries BlockDeviceRegistry__metadata_entries()
{
    return EmptyArray(BlockDeviceMetadataEntries);
}

Optional(BlockDeviceMetadata) BlockDeviceRegistry__metadata(ConstBStringPtr alias) __attribute__((weak));
Optional(BlockDeviceMetadata) BlockDeviceRegistry__metadata(ConstBStringPtr alias)
{
    return None(BlockDeviceMetadata);
}

static BString DevFilesystemIO__name_from_path(ConstBStringPtr path)
{
    BStringArray segments = BString__split(path, '/');
    if (BStringArray__len(&segments) == 0)
    {
        return *path;
    }

    match_optional(BString, BStringArray__at(&segments, BStringArray__len(&segments) - 1),
        some(name)
        {
            return name;
        },
        none() { return *path; }
    );
}

static Result(IOInternalHandle) DevFilesystemIOState__add_handle(DevFilesystemIOStatePtr self, ConstBStringPtr path)
{
    critical_section(self->state.lock,
    {
        IOInternalHandle handle = self->next_handle++ | self->state.filesystem->fs_code;
        FileHandleTargetPtr target = FileHandleTarget__heap_new(path);
        FileHandleTargetMap__set(&self->state.handles, handle, target);
        cs_return Success(IOInternalHandle, handle);
    });
}

static Result(FileHandleTargetPtr) DevFilesystemIOState__get_handle(DevFilesystemIOStatePtr self, IOInternalHandle handle)
{
    critical_section(self->state.lock,
    {
        match_optional(FileHandleTargetPtr, FileHandleTargetMap__get(&self->state.handles, handle),
            some(target) { cs_return Success(FileHandleTargetPtr, target); },
            none() { cs_return Error(FileHandleTargetPtr, invalid_handle, IO_UNKNOWN_HANDLE); }
        );
    });

    return Error(FileHandleTargetPtr, invalid_handle, IO_UNKNOWN_HANDLE);
}

static Result(IOInternalHandle) DevFilesystemIOState__delete_handle(DevFilesystemIOStatePtr self, IOInternalHandle handle)
{
    critical_section(self->state.lock,
    {
        match_optional(FileHandleTargetPtr, FileHandleTargetMap__get(&self->state.handles, handle),
            some(target)
            {
                FileHandleTarget__dispose(target);
                FileHandleTargetMap__delete(&self->state.handles, handle);
                cs_return Success(IOInternalHandle, handle);
            },
            none() { cs_return Error(IOInternalHandle, invalid_handle, IO_UNKNOWN_HANDLE); }
        );
    });

    return Error(IOInternalHandle, invalid_handle, IO_UNKNOWN_HANDLE);
}

static Result(IOInternalHandle) DevFilesystemIO__open(IOStatePtr self, ConstBStringPtr path, Flags flags)
{
    DevFilesystemIOStatePtr fs_state = (DevFilesystemIOStatePtr) self;

    match_optional(BlockDeviceMetadata, BlockDeviceRegistry__metadata(path),
        some(metadata) { return DevFilesystemIOState__add_handle(fs_state, path); },
        none_do_nothing()
    );

    return Error(IOInternalHandle, invalid_path, IO_INVALID_PATH);
}

static Result(IOInternalHandle) DevFilesystemIO__close(IOStatePtr self, IOInternalHandle handle)
{
    return DevFilesystemIOState__delete_handle((DevFilesystemIOStatePtr) self, handle);
}

static Result(IOBufferPtr) DevFilesystemIO__read(
    IOStatePtr self, IOInternalHandle handle, size_t size, ConstExternalBufferPtr target_buffer
)
{
    DevFilesystemIOStatePtr fs_state = (DevFilesystemIOStatePtr) self;

    match_result(FileHandleTargetPtr, DevFilesystemIOState__get_handle(fs_state, handle),
        success(target)
        {
            match_optional(BlockDeviceMetadata, BlockDeviceRegistry__metadata(&target->path),
                some(metadata)
                {
                    BString text = BString__sprintf(
                        &metadata_format,
                        &metadata.path,
                        (int) metadata.block_size,
                        (int) metadata.block_count,
                        (int) metadata.userspace_service
                    );

                    if (target->offset >= (FilePointer) BString__length(&text))
                    {
                        return Error(IOBufferPtr, invalid_path, IO_EOF);
                    }

                    size_t remaining = (size_t) BString__length(&text) - target->offset;
                    size_t read_size = remaining < size ? remaining : size;
                    IOBuffer__reset(&target->buffer);
                    IOBuffer__push_all(&target->buffer, ptr(BString__cstr(&text) + target->offset, Char), read_size);
                    target->offset += read_size;
                    return Success(IOBufferPtr, &target->buffer);
                },
                none_do_nothing()
            );

            return Error(IOBufferPtr, invalid_path, IO_INVALID_PATH);
        },
        error(code, message) { return Error(IOBufferPtr, message, code); }
    );

    return Error(IOBufferPtr, invalid_handle, IO_UNKNOWN_HANDLE);
}

static Result(FilesystemNodes) DevFilesystemIO__list_nodes(IOStatePtr self, FilesystemNodePtr parent, ConstBStringPtr path)
{
    BStringArray path_segments = BString__split(path, '/');
    if (BStringArray__len(&path_segments) != 0)
    {
        return Error(FilesystemNodes, invalid_path, IO_INVALID_PATH);
    }

    DevFilesystemIOStatePtr fs_state = (DevFilesystemIOStatePtr) self;
    FilesystemNodes result = EmptyArray(FilesystemNodes);
    BlockDevicePaths paths = BlockDeviceRegistry__paths();
    BlockDeviceMetadataEntries metadata_entries = BlockDeviceRegistry__metadata_entries();

    match_result(CollectionCode, IOSubsystem__append_dev_filesystem_nodes(
        fs_state->io_subsystem, fs_state->state.filesystem, &result
    ),
        success_do_nothing(),
        error(code, message) { return Error(FilesystemNodes, message, code); }
    );

    for_each_fa(BlockDevicePaths, paths, index, device_path,
    {
        BString name = DevFilesystemIO__name_from_path(&device_path);
        FilesystemNodes__push(
            &result,
            FilesystemNode__heap_new(fs_state->state.filesystem, &name, 0)
        );

    });
    for_each_fa(BlockDeviceMetadataEntries, metadata_entries, index, metadata,
    {
        FilesystemNodes__push(
            &result,
            FilesystemNode__heap_new(fs_state->state.filesystem, &metadata.alias, 0)
        );

    });
    return Success(FilesystemNodes, result);
}

FileIO DevFilesystemIO__new()
{
    return (FileIO) {
        .base_io = IO__new(
            DevFilesystemIO__open,
            DevFilesystemIO__read,
            IO__unsupported_write,
            IO__unsupported_accept,
            DevFilesystemIO__close,
            IO__unsupported_park,
            IO__unsupported_unpark,
            IO__unsupported_server_buffer,
            IO__unsupported_server_write_buffer,
            IO__unsupported_data_available,
            IO__unsupported_data_consumed,
            IO__unsupported_eof
        ),
        .list_nodes = DevFilesystemIO__list_nodes,
        .list_dir = FileIO__unsupported_list_dir,
        .mkdir = FileIO__unsupported_mkdir,
        .remove = FileIO__unsupported_remove,
        .link = FileIO__unsupported_link,
    };
}

DevFilesystemIOStatePtr DevFilesystemIOState__heap_new(FilesystemPtr fs, IOSubsystemPtr io_subsystem)
{
    DevFilesystemIOStatePtr state = ptr(Kernel__alloc_kernel(sizeof(DevFilesystemIOState)), DevFilesystemIOState);
    *state = (DevFilesystemIOState) {
        .state = FilesystemIOState__new(fs),
        .io_subsystem = io_subsystem,
        .next_handle = 1,
    };
    fs->state = &state->state;
    return state;
}
