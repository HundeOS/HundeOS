#include "filesystem.h"
#include "../../../kalloc.h"

MapBodyDefine(Mounts);
MapBodyDefine(FileHandleTargetMap);

static KString invalid_path_format = KSTRING_NEW("Invalid path %s");
static KString not_implemented = KSTRING_NEW("Operation not implemented");
static KString invalid_handle = KSTRING_NEW("Unknown handle");

#define CODE_SHIFT 20
static const FilesystemCode FS_CODE_MASK = (0xFFFFULL << CODE_SHIFT);

typedef struct MountRoute
{
    FilesystemPtr filesystem;
    KString mount_path;
    KString subpath;
}
MountRoute;

OptionalDef(MountRoute);

static bool Filesystem__mount_matches(KStringPtr path, KStringPtr mount_path)
{
    KStringArray path_segments = KString__split(path, '/');
    KStringArray mount_segments = KString__split(mount_path, '/');

    if (KStringArray__len(&mount_segments) > KStringArray__len(&path_segments))
    {
        return false;
    }

    for (size_t i = 0; i < KStringArray__len(&mount_segments); i++)
    {
        KString path_segment = UnwrapPanic(KString, KStringArray__at(&path_segments, i));
        KString mount_segment = UnwrapPanic(KString, KStringArray__at(&mount_segments, i));
        if (!KString__equals(&path_segment, &mount_segment))
        {
            return false;
        }
    }

    return true;
}

static KString Filesystem__subpath(KStringPtr path, KStringPtr mount_path)
{
    KStringArray path_segments = KString__split(path, '/');
    KStringArray mount_segments = KString__split(mount_path, '/');
    KStringArray subpath_segments = EmptyArray(KStringArray);

    for (size_t i = KStringArray__len(&mount_segments); i < KStringArray__len(&path_segments); i++)
    {
        KStringArray__push(&subpath_segments, UnwrapPanic(KString, KStringArray__at(&path_segments, i)));
    }

    return KString__join(&subpath_segments, '/');
}

static Optional(MountRoute) Filesystem__find_mount(FilesystemPtr self, KString path)
{
    Optional(MountRoute) best = None(MountRoute);
    size_t best_len = 0;

    MapIterate(Mounts, self->mounts, index, mount_path, mounted_fs)
        if (Filesystem__mount_matches(&path, &mount_path) && mount_path.len > best_len)
        {
            best_len = mount_path.len;
            best = Some(MountRoute, ((MountRoute) {
                .filesystem = mounted_fs,
                .mount_path = mount_path,
                .subpath = Filesystem__subpath(&path, &mount_path),
            }));
        }
    End

    return best;
}

static Result(IOInternalHandle) RootFileIO__delegate_close(FilesystemIOStatePtr self, IOInternalHandle handle)
{
    FilesystemCode code = (handle & FS_CODE_MASK);
    MapIterate(Mounts, self->filesystem->mounts, index, name, filesys)
        if (filesys->fs_code == code)
        {
            return filesys->file_io.base_io.close(&filesys->state->state, handle);
        }
    End

    return Error(IOInternalHandle, invalid_handle, IO_UNKNOWN_HANDLE);
}

static Result(ByteCount) RootFileIO__delegate_write(
    FilesystemIOStatePtr self, IOInternalHandle handle, DataPtr data, size_t size
)
{
    FilesystemCode code = (handle & FS_CODE_MASK);
    MapIterate(Mounts, self->filesystem->mounts, index, name, filesys)
        if (filesys->fs_code == code)
        {
            return filesys->file_io.base_io.write(&filesys->state->state, handle, data, size);
        }
    End

    return Error(ByteCount, invalid_handle, IO_UNKNOWN_HANDLE);
}

Result(IOInternalHandle) RootFileIO__open(IOStatePtr self, KString path, Flags flags)
{
    FilesystemIOStatePtr fs_state = (FilesystemIOStatePtr) self;
    Match(MountRoute, Filesystem__find_mount(fs_state->filesystem, path))
        IsSome(route,
        {
            __d_printf(FILESYSTEM, "mount found = %s", cstr(route.mount_path));
            return route.filesystem->file_io.base_io.open(
                &route.filesystem->state->state, route.subpath, flags
            );
        })
        IsNone(return Error(IOInternalHandle, KString__sprintf(&invalid_path_format, &path), IO_INVALID_PATH))
    End

    return Error(IOInternalHandle, KString__sprintf(&invalid_path_format, &path), IO_INVALID_PATH);
}

Result(IOInternalHandle) RootFileIO__close(IOStatePtr self, IOInternalHandle handle)
{
    FilesystemIOStatePtr fs_state = (FilesystemIOStatePtr) self;
    return RootFileIO__delegate_close(fs_state, handle);
}

Result(IOBufferPtr) RootFileIO__read(IOStatePtr self, IOInternalHandle handle, size_t size)
{
    FilesystemIOStatePtr fs_state = (FilesystemIOStatePtr) self;
    FilesystemCode code = (handle & FS_CODE_MASK);
    MapIterate(Mounts, fs_state->filesystem->mounts, index, name, filesys)
        if (filesys->fs_code == code)
        {
            return filesys->file_io.base_io.read(&filesys->state->state, handle, size);
        }
    End

    return Error(IOBufferPtr, invalid_handle, IO_UNKNOWN_HANDLE);
}

Result(ByteCount) RootFileIO__write(IOStatePtr self, IOInternalHandle handle, DataPtr data, size_t size)
{
    FilesystemIOStatePtr fs_state = (FilesystemIOStatePtr) self;
    return RootFileIO__delegate_write(fs_state, handle, data, size);
}

Result(ParkStatus) RootFileIO__park(IOStatePtr self, IOContextPtr parked_context)
{
    (void) self;
    (void) parked_context;
    return Error(ParkStatus, not_implemented, IO_OPERATION_NOT_IMPLEMENTED);
}

Result(ParkStatus) RootFileIO__unpark(IOStatePtr self)
{
    (void) self;
    return Error(ParkStatus, not_implemented, IO_OPERATION_NOT_IMPLEMENTED);
}

Result(FilesystemNodes) RootFileIO__list_nodes(IOStatePtr self, FilesystemNodePtr parent, KString path)
{
    (void) parent;
    FilesystemNodes result = EmptyArray(FilesystemNodes);

    FilesystemIOStatePtr fs_state = (FilesystemIOStatePtr) self;
    FilesystemPtr filesystem = fs_state->filesystem;
    KStringArray path_segments = KString__split(&path, '/');

    if (KStringArray__len(&path_segments) == 0)
    {
        MapIterate(Mounts, filesystem->mounts, index, mount_path, mount_fs)
            KStringArray mount_segments = KString__split(&mount_path, '/');
            KString top_level = UnwrapPanic(KString, KStringArray__at(&mount_segments, 0));

            bool already_added = false;
            FAIterate(FilesystemNodes, result, node_index, node)
                if (KString__equals(&node->name, &top_level))
                {
                    already_added = true;
                }
            End

            if (!already_added)
            {
                FilesystemNodes__push(&result, FilesystemNode__heap_new(mount_fs, top_level, 0));
            }
        End
    }
    else
    {
        Match(MountRoute, Filesystem__find_mount(filesystem, path))
            IsSome(route,
            {
                return route.filesystem->file_io.list_nodes(
                    &route.filesystem->state->state, filesystem->root, route.subpath
                );
            })
            IsNone(
            {
                MapIterate(Mounts, filesystem->mounts, index, mount_path, mount_fs)
                    KStringArray mount_segments = KString__split(&mount_path, '/');
                    if (KStringArray__len(&mount_segments) <= KStringArray__len(&path_segments))
                    {
                        continue;
                    }

                    bool prefix_matches = true;
                    for (size_t i = 0; i < KStringArray__len(&path_segments); i++)
                    {
                        KString path_segment = UnwrapPanic(KString, KStringArray__at(&path_segments, i));
                        KString mount_segment = UnwrapPanic(KString, KStringArray__at(&mount_segments, i));
                        if (!KString__equals(&path_segment, &mount_segment))
                        {
                            prefix_matches = false;
                        }
                    }

                    if (prefix_matches)
                    {
                        KString child = UnwrapPanic(KString, KStringArray__at(&mount_segments, KStringArray__len(&path_segments)));
                        bool already_added = false;
                        FAIterate(FilesystemNodes, result, node_index, node)
                            if (KString__equals(&node->name, &child))
                            {
                                already_added = true;
                            }
                        End

                        if (!already_added)
                        {
                            FilesystemNodes__push(&result, FilesystemNode__heap_new(mount_fs, child, 0));
                        }
                    }
                End

                if (FilesystemNodes__len(&result) == 0)
                {
                    return Error(FilesystemNodes, KString__sprintf(&invalid_path_format, &path), IO_INVALID_PATH);
                }
            })
        End
    }

    return Success(FilesystemNodes, result);
}

Result(IOStatus) RootFileIO__mkdir(IOStatePtr self, KString path)
{
    FilesystemIOStatePtr fs_state = (FilesystemIOStatePtr) self;
    Match(MountRoute, Filesystem__find_mount(fs_state->filesystem, path))
        IsSome(route, return route.filesystem->file_io.mkdir(&route.filesystem->state->state, route.subpath))
        IsNone(return Error(IOStatus, KString__sprintf(&invalid_path_format, &path), IO_INVALID_PATH))
    End

    return Error(IOStatus, KString__sprintf(&invalid_path_format, &path), IO_INVALID_PATH);
}

Result(IOStatus) RootFileIO__remove(IOStatePtr self, KString path, Flags flags)
{
    FilesystemIOStatePtr fs_state = (FilesystemIOStatePtr) self;
    Match(MountRoute, Filesystem__find_mount(fs_state->filesystem, path))
        IsSome(route, return route.filesystem->file_io.remove(&route.filesystem->state->state, route.subpath, flags))
        IsNone(return Error(IOStatus, KString__sprintf(&invalid_path_format, &path), IO_INVALID_PATH))
    End

    return Error(IOStatus, KString__sprintf(&invalid_path_format, &path), IO_INVALID_PATH);
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
            .open = RootFileIO__open,
            .close = RootFileIO__close,
            .read = RootFileIO__read,
            .write = RootFileIO__write,
            .park = RootFileIO__park,
            .unpark = RootFileIO__unpark,
        },
        .list_nodes = RootFileIO__list_nodes,
        .mkdir = RootFileIO__mkdir,
        .remove = RootFileIO__remove,
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
    __d_printf(FILESYSTEM, "Created filesystem %x %p with code %x", sizeof(Filesystem), fs, code);
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
    (void) self;
    // TODO we need to implement a free
}
