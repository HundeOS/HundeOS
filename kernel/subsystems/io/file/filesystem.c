#include "filesystem.h"

MapBodyDefine(Mounts);
MapBodyDefine(FileHandleTargetMap);

static KString invalid_path_format = KSTRING_NEW("Invalid path %s");
static KString not_implemented = KSTRING_NEW("Operation not implemented");
static KString invalid_handle = KSTRING_NEW("Unknown handle");

#define CODE_SHIFT 20
static const FilesystemCode FS_CODE_MASK = (0xFFFF << CODE_SHIFT);

Result(IOInternalHandle) RootFileIO_open(IOStatePtr state, KString path, Flags flags)
{
    FilesystemIOStatePtr fs_state = (FilesystemIOStatePtr) state;
    KStringArray path_segments = KString__split(&path, '/');
    Optional(KString) top_level_opt = KStringArray__pop_front(&path_segments);
    KString remaining_path = KString__join(&path_segments, '/');

    Match(KString, top_level_opt)
        IsNone(return Error(IOInternalHandle, KString__sprintf(&invalid_path_format, &path), IO_INVALID_PATH));
        IsSome(top_level,
        {
            Optional(FilesystemPtr) mount = Mounts__get(&fs_state->filesystem->mounts, top_level);
            Match(FilesystemPtr, mount)
                IsSome(filesystem,
                {
                    return filesystem->file_io.base_io.open(&filesystem->state->state, remaining_path, flags);
                });
                IsNone(
                {
                    return Error(IOInternalHandle, KString__sprintf(&invalid_path_format, &path), IO_INVALID_PATH);
                });
            End
        });
    End

    return Error(IOInternalHandle, KString__sprintf(&invalid_path_format, &path), IO_INVALID_PATH);
}

Result(IOInternalHandle) RootFileIO_close(IOStatePtr state, IOInternalHandle handle)
{
    return Error(IOInternalHandle, not_implemented, IO_OPERATION_NOT_IMPLEMENTED);
}

Result(IOBufferPtr) RootFileIO_read(IOStatePtr state, IOInternalHandle handle, size_t size)
{
    FilesystemIOStatePtr fs_state = (FilesystemIOStatePtr) state;
    FilesystemCode code = (handle & FS_CODE_MASK);
    MapIterate(Mounts, fs_state->filesystem->mounts, index, name, filesys)
        if (filesys->fs_code == code)
        {
            return filesys->file_io.base_io.read(&filesys->state->state, handle, size);
        }
    End

    return Error(IOBufferPtr, invalid_handle, IO_UNKNOWN_HANDLE);
}

Result(ByteCount) RootFileIO_write(IOStatePtr state, IOInternalHandle handle, DataPtr data, size_t size)
{
    return Error(ByteCount, not_implemented, IO_OPERATION_NOT_IMPLEMENTED);
}

Result(ParkStatus) RootFileIO_park(IOStatePtr state, IOContext parked_context)
{
    return Error(ParkStatus, not_implemented, IO_OPERATION_NOT_IMPLEMENTED);
}

Result(ParkStatus) RootFileIO_unpark(IOStatePtr state)
{
    return Error(ParkStatus, not_implemented, IO_OPERATION_NOT_IMPLEMENTED);
}

Result(FilesystemNodes) RootFileIO_list_nodes(IOStatePtr state, FilesystemNodePtr parent, KString path)
{
    FilesystemNodes result = EmptyArray(FilesystemNodes);

    FilesystemIOStatePtr fs_state = (FilesystemIOStatePtr) state;
    FilesystemPtr filesystem = fs_state->filesystem;
    KStringArray path_segments = KString__split(&path, '/');

    if (KStringArray__len(&path_segments) == 0)
    {
        // list the mounts
        for (int i = 0; i < Mounts__len(&filesystem->mounts); i++)
        {
            MapKeyPtrMatch(Mounts, Mounts__key(&filesystem->mounts, i))
                IsSome(mount_path,
                {
                    Match(FilesystemPtr, Mounts__get(&filesystem->mounts, *mount_path))
                        IsSome(mount_fs,
                        {
                            FilesystemNodes__push(&result, FilesystemNode__heap_new(mount_fs, *mount_path, 0));
                        });
                        IsNone(
                        {
                            return Error(FilesystemNodes, KString__sprintf(&invalid_path_format, &path), IO_INVALID_MOUNT_POINT);
                        })
                    End
                });
                IsNone(
                {
                    return Error(FilesystemNodes, KString__sprintf(&invalid_path_format, &path), IO_INVALID_PATH);
                })
            End
        }
    }
    else
    {
        Match(KString, KStringArray__pop_front(&path_segments))
            IsSome(segment,
            {
                // KStringArray remaining_segments = KStringArray__remove_at(&path_segments, 0);
                Match(FilesystemPtr, Mounts__get(&filesystem->mounts, segment))
                    IsSome(mounted_fs,
                    {
                        KString subpath = KString__join(&path_segments, '/');
                        return mounted_fs->file_io.list_nodes(&mounted_fs->state->state, filesystem->root, subpath);
                    });
                    IsNone(
                    {
                        return Error(FilesystemNodes, KString__sprintf(&invalid_path_format, &path), IO_INVALID_PATH);
                    })
                End
            });
            IsNone(return Error(FilesystemNodes, KString__sprintf(&invalid_path_format, &path), IO_INVALID_PATH))
        End
    }

    return Success(FilesystemNodes, result);
}


FilesystemIOState FilesystemIOState__new(FilesystemPtr filesystem)
{
    FilesystemIOState state = (FilesystemIOState) {
        .state = NEW_IO_STATE,
        .filesystem = filesystem,
        .handles = EmptyMap(FileHandleTargetMap),
        .lock = MUTEX_INIT,
    };

    return state;
}

FilesystemIOStatePtr FilesystemIOState__heap_new(FilesystemPtr filesystem)
{
    FilesystemIOStatePtr state = ptr(Kernel__alloc_kernel(sizeof(FilesystemIOState)), FilesystemIOState);
    *state = FilesystemIOState__new(filesystem);
    filesystem->state = state;
    return state;
}

FileIO RootFilesystemIO__new()
{
    FileIO root_file_io = (FileIO) {
        .base_io = (IO) {
            .open = RootFileIO_open,
            .close = RootFileIO_close,
            .read = RootFileIO_read,
            .write = RootFileIO_write,
            .park = RootFileIO_park,
            .unpark = RootFileIO_unpark,
        },
        .list_nodes = RootFileIO_list_nodes,
    };

    return root_file_io;
}

static FilesystemCode fs_codes = 0x01;


FilesystemPtr Filesystem__heap_new(KString name, FileIO file_io)
{
    FilesystemPtr fs = ptr(Kernel__alloc_kernel(sizeof(Filesystem)), Filesystem);
    FilesystemCode code = fs_codes++;

    *fs = (Filesystem) {
        .name = name,
        .mounts = Mounts__new(),
        .file_io = file_io,
        .fs_code = (code << CODE_SHIFT),
    };

    return fs;
}

FilesystemPtr RootFilesystem__heap_new()
{
    return Filesystem__heap_new(kstring("root"), RootFilesystemIO__new());
}

Result(MountResult) Filesystem__mount(FilesystemPtr self, KString path, FilesystemPtr filesystem)
{
    Mounts__set(&self->mounts, path, filesystem);

    return Success(MountResult, MOUNT_OK);
}

FileHandleTargetPtr FileHandleTarget__heap_new(KString path)
{
    FileHandleTargetPtr target = ptr(Kernel__alloc_kernel(sizeof(FileHandleTarget)), FileHandleTarget);
    *target = (FileHandleTarget) {
        .path = path,
        .offset = 0,
        .buffer = EmptyRingBuffer(IOBuffer),
    };

    return target;
}

void FileHandleTarget__dispose(FileHandleTargetPtr self)
{
    // TODO we need to implement a free
}