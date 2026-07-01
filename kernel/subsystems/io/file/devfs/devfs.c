#include "devfs.h"

#include "../../../../kalloc.h"
#include "../../block/block_device.h"

static KString invalid_path = KSTRING_NEW("Invalid devfs path");
static KString not_supported = KSTRING_NEW("Devfs operation not supported");

BlockDevicePaths BlockDeviceRegistry__paths() __attribute__((weak));
BlockDevicePaths BlockDeviceRegistry__paths()
{
    return EmptyArray(BlockDevicePaths);
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

static Result(IOInternalHandle) DevFilesystemIO__open(IOStatePtr self, KString path, Flags flags)
{
    return Error(IOInternalHandle, not_supported, IO_OPERATION_NOT_SUPPORTED);
}

static Result(IOInternalHandle) DevFilesystemIO__close(IOStatePtr self, IOInternalHandle handle)
{
    return Error(IOInternalHandle, not_supported, IO_OPERATION_NOT_SUPPORTED);
}

static Result(IOBufferPtr) DevFilesystemIO__read(
    IOStatePtr self, IOInternalHandle handle, size_t size, ExternalBuffer target_buffer
)
{
    return Error(IOBufferPtr, not_supported, IO_OPERATION_NOT_SUPPORTED);
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

    FAIterate(BlockDevicePaths, paths, index, device_path)
        FilesystemNodes__push(
            &result,
            FilesystemNode__heap_new(fs_state->state.filesystem, DevFilesystemIO__name_from_path(device_path), 0)
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
    };
}

DevFilesystemIOStatePtr DevFilesystemIOState__heap_new(FilesystemPtr fs)
{
    DevFilesystemIOStatePtr state = ptr(Kernel__alloc_kernel(sizeof(DevFilesystemIOState)), DevFilesystemIOState);
    *state = (DevFilesystemIOState) {
        .state = FilesystemIOState__new(fs),
    };
    fs->state = &state->state;
    return state;
}
