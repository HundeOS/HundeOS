#include "devfs.h"

#include "../../../../kalloc.h"
#include "../../block/block_device.h"

static KString invalid_path = KSTRING_NEW("Invalid devfs path");
static KString not_supported = KSTRING_NEW("Devfs operation not supported");
static KString invalid_handle = KSTRING_NEW("Invalid devfs handle");
static KString metadata_format = KSTRING_NEW("path=%s\nblock_size=%i\nblock_count=%i\nuserspace_service=%i\n");

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

Optional(BlockDeviceMetadata) BlockDeviceRegistry__metadata(KString alias) __attribute__((weak));
Optional(BlockDeviceMetadata) BlockDeviceRegistry__metadata(KString alias)
{
    return None(BlockDeviceMetadata);
}

static KString DevFilesystemIO__name_from_path(KString path)
{
    KStringArray segments = KString__split(&path, '/');
    if (KStringArray__len(&segments) == 0)
    {
        return path;
    }

    return Unwrap(KString, KStringArray__at(&segments, KStringArray__len(&segments) - 1), path);
}

static Result(IOInternalHandle) DevFilesystemIOState__add_handle(DevFilesystemIOStatePtr self, KString path)
{
    CriticalSection(self->state.lock,
    {
        IOInternalHandle handle = self->next_handle++ | self->state.filesystem->fs_code;
        FileHandleTargetPtr target = FileHandleTarget__heap_new(path);
        FileHandleTargetMap__set(&self->state.handles, handle, target);
        _cs_return Success(IOInternalHandle, handle);
    })
}

static Result(FileHandleTargetPtr) DevFilesystemIOState__get_handle(DevFilesystemIOStatePtr self, IOInternalHandle handle)
{
    CriticalSection(self->state.lock,
    {
        Match(FileHandleTargetPtr, FileHandleTargetMap__get(&self->state.handles, handle))
            IsSome(target, _cs_return Success(FileHandleTargetPtr, target))
            IsNone(_cs_return Error(FileHandleTargetPtr, invalid_handle, IO_UNKNOWN_HANDLE))
        End
    })

    return Error(FileHandleTargetPtr, invalid_handle, IO_UNKNOWN_HANDLE);
}

static Result(IOInternalHandle) DevFilesystemIOState__delete_handle(DevFilesystemIOStatePtr self, IOInternalHandle handle)
{
    CriticalSection(self->state.lock,
    {
        Match(FileHandleTargetPtr, FileHandleTargetMap__get(&self->state.handles, handle))
            IsSome(target,
            {
                FileHandleTarget__dispose(target);
                FileHandleTargetMap__delete(&self->state.handles, handle);
                _cs_return Success(IOInternalHandle, handle);
            })
            IsNone(_cs_return Error(IOInternalHandle, invalid_handle, IO_UNKNOWN_HANDLE))
        End
    })

    return Error(IOInternalHandle, invalid_handle, IO_UNKNOWN_HANDLE);
}

static Result(IOInternalHandle) DevFilesystemIO__open(IOStatePtr self, KString path, Flags flags)
{
    DevFilesystemIOStatePtr fs_state = (DevFilesystemIOStatePtr) self;

    Match(BlockDeviceMetadata, BlockDeviceRegistry__metadata(path))
        IsSome(metadata, return DevFilesystemIOState__add_handle(fs_state, path))
    End

    return Error(IOInternalHandle, invalid_path, IO_INVALID_PATH);
}

static Result(IOInternalHandle) DevFilesystemIO__close(IOStatePtr self, IOInternalHandle handle)
{
    return DevFilesystemIOState__delete_handle((DevFilesystemIOStatePtr) self, handle);
}

static Result(IOBufferPtr) DevFilesystemIO__read(
    IOStatePtr self, IOInternalHandle handle, size_t size, ExternalBuffer target_buffer
)
{
    DevFilesystemIOStatePtr fs_state = (DevFilesystemIOStatePtr) self;

    ResultMatch(FileHandleTargetPtr, DevFilesystemIOState__get_handle(fs_state, handle))
        IsSuccess(target,
        {
            Match(BlockDeviceMetadata, BlockDeviceRegistry__metadata(target->path))
                IsSome(metadata,
                {
                    KString text = KString__sprintf(
                        &metadata_format,
                        cstr(metadata.path),
                        (int) metadata.block_size,
                        (int) metadata.block_count,
                        (int) metadata.userspace_service
                    );

                    if (target->offset >= (FilePointer) text.len)
                    {
                        return Error(IOBufferPtr, invalid_path, IO_EOF);
                    }

                    size_t remaining = (size_t) text.len - target->offset;
                    size_t read_size = remaining < size ? remaining : size;
                    IOBuffer__reset(&target->buffer);
                    IOBuffer__push_all(&target->buffer, ptr(&text.data[target->offset], Char), read_size);
                    target->offset += read_size;
                    return Success(IOBufferPtr, &target->buffer);
                })
            End

            return Error(IOBufferPtr, invalid_path, IO_INVALID_PATH);
        })
        IsError(code, message, return Error(IOBufferPtr, message, code))
    End

    return Error(IOBufferPtr, invalid_handle, IO_UNKNOWN_HANDLE);
}

static Result(ByteCount) DevFilesystemIO__write(IOStatePtr self, IOInternalHandle handle, DataPtr data, size_t size)
{
    return Error(ByteCount, not_supported, IO_OPERATION_NOT_SUPPORTED);
}

static Result(ParkStatus) DevFilesystemIO__park(IOStatePtr self, IOContextPtr parked_context)
{
    return Error(ParkStatus, not_supported, IO_OPERATION_NOT_SUPPORTED);
}

static Result(ParkStatus) DevFilesystemIO__unpark(IOStatePtr self)
{
    return Error(ParkStatus, not_supported, IO_OPERATION_NOT_SUPPORTED);
}

static Result(FilesystemNodes) DevFilesystemIO__list_nodes(IOStatePtr self, FilesystemNodePtr parent, KString path)
{
    KStringArray path_segments = KString__split(&path, '/');
    if (KStringArray__len(&path_segments) != 0)
    {
        return Error(FilesystemNodes, invalid_path, IO_INVALID_PATH);
    }

    DevFilesystemIOStatePtr fs_state = (DevFilesystemIOStatePtr) self;
    FilesystemNodes result = EmptyArray(FilesystemNodes);
    BlockDevicePaths paths = BlockDeviceRegistry__paths();
    BlockDeviceMetadataEntries metadata_entries = BlockDeviceRegistry__metadata_entries();

    FAIterate(BlockDevicePaths, paths, index, device_path)
        FilesystemNodes__push(
            &result,
            FilesystemNode__heap_new(fs_state->state.filesystem, DevFilesystemIO__name_from_path(device_path), 0)
        );
    End

    FAIterate(BlockDeviceMetadataEntries, metadata_entries, index, metadata)
        FilesystemNodes__push(
            &result,
            FilesystemNode__heap_new(fs_state->state.filesystem, metadata.alias, 0)
        );
    End

    return Success(FilesystemNodes, result);
}

static Result(IOStatus) DevFilesystemIO__mkdir(IOStatePtr self, KString path)
{
    return Error(IOStatus, not_supported, IO_OPERATION_NOT_SUPPORTED);
}

static Result(IOStatus) DevFilesystemIO__remove(IOStatePtr self, KString path, Flags flags)
{
    return Error(IOStatus, not_supported, IO_OPERATION_NOT_SUPPORTED);
}

static Result(IOStatus) DevFilesystemIO__link(IOStatePtr self, KString source_path, KString target_path)
{
    return Error(IOStatus, not_supported, IO_OPERATION_NOT_SUPPORTED);
}

FileIO DevFilesystemIO__new()
{
    return (FileIO) {
        .base_io = (IO) {
            .open = DevFilesystemIO__open,
            .close = DevFilesystemIO__close,
            .read = DevFilesystemIO__read,
            .write = DevFilesystemIO__write,
            .park = DevFilesystemIO__park,
            .unpark = DevFilesystemIO__unpark,
        },
        .list_nodes = DevFilesystemIO__list_nodes,
        .mkdir = DevFilesystemIO__mkdir,
        .remove = DevFilesystemIO__remove,
        .link = DevFilesystemIO__link,
    };
}

DevFilesystemIOStatePtr DevFilesystemIOState__heap_new(FilesystemPtr fs)
{
    DevFilesystemIOStatePtr state = ptr(Kernel__alloc_kernel(sizeof(DevFilesystemIOState)), DevFilesystemIOState);
    *state = (DevFilesystemIOState) {
        .state = FilesystemIOState__new(fs),
        .next_handle = 1,
    };
    fs->state = &state->state;
    return state;
}
