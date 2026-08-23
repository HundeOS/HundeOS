#include "filesystem.h"
#include "../../../kalloc.h"
#include "../block/block_device.h"

MapBodyDefine(Mounts);
MapBodyDefine(FileHandleTargetMap);
LinkedListBodyDefine(FilesystemTableEntries);

static BString invalid_path_format = BSTRING_NEW("Invalid path %s");
static BString not_implemented = BSTRING_NEW("Operation not implemented");
static BString invalid_handle = BSTRING_NEW("Unknown handle");
static BString root_path = BSTRING_NEW("/");

#define CODE_SHIFT 20
static const FilesystemCode FS_CODE_MASK = (0xFFFFULL << CODE_SHIFT);

bool FilesystemTableEntry__equals(FilesystemTableEntryPtr self, FilesystemTableEntryPtr other)
{
    return BString__equals(&self->device_path, &other->device_path)
        && BString__equals(&self->filesystem_type, &other->filesystem_type)
        && BString__equals(&self->mountpoint, &other->mountpoint)
        && self->match_block_count == other->match_block_count
        && self->block_count == other->block_count;
}

FilesystemTable FilesystemTable__new()
{
    return (FilesystemTable) {
        .entries = FilesystemTableEntries__new(),
    };
}

Result(CollectionCode) FilesystemTable__add(
    FilesystemTablePtr self, BString device_path, BString filesystem_type, BString mountpoint
)
{
    return FilesystemTableEntries__push(&self->entries, ((FilesystemTableEntry) {
        .device_path = device_path,
        .filesystem_type = filesystem_type,
        .mountpoint = mountpoint,
        .match_block_count = false,
        .block_count = 0,
    }));
}

Result(CollectionCode) FilesystemTable__add_by_block_count(
    FilesystemTablePtr self, uint64_t block_count, BString filesystem_type, BString mountpoint
)
{
    return FilesystemTableEntries__push(&self->entries, ((FilesystemTableEntry) {
        .device_path = EMPTY_BSTRING,
        .filesystem_type = filesystem_type,
        .mountpoint = mountpoint,
        .match_block_count = true,
        .block_count = block_count,
    }));
}

typedef struct MountRoute
{
    FilesystemPtr filesystem;
    BString mount_path;
    BString subpath;
}
MountRoute;

OptionalDef(MountRoute);

typedef struct RootAsyncListDirContext
{
    FilesystemPtr filesystem;
    BString path;
    DirectoryEntryPtr entries;
    size_t max_entries;
    Optional(KernelResultMapper) parent_mapper;
    Anything parent_mapper_context;
}
RootAsyncListDirContext;

typedef RootAsyncListDirContext * RootAsyncListDirContextPtr;

static Result(Anything) RootFileIO__complete_async_list_dir(Result(Anything) result, Anything raw_context);

static bool Filesystem__mount_matches(BStringPtr path, BStringPtr mount_path)
{
    BStringArray path_segments = BString__split(path, '/');
    BStringArray mount_segments = BString__split(mount_path, '/');

    if (BStringArray__len(&mount_segments) > BStringArray__len(&path_segments))
    {
        return false;
    }

    for (size_t i = 0; i < BStringArray__len(&mount_segments); i++)
    {
        BString path_segment = EMPTY_BSTRING;
        BString mount_segment = EMPTY_BSTRING;
        match_optional(BString, BStringArray__at(&path_segments, i),
            some(segment) { path_segment = segment; },
            none() { return false; }
        )
        match_optional(BString, BStringArray__at(&mount_segments, i),
            some(segment) { mount_segment = segment; },
            none() { return false; }
        )
        if (!BString__equals(&path_segment, &mount_segment))
        {
            return false;
        }
    }

    return true;
}

static BString Filesystem__subpath(BStringPtr path, BStringPtr mount_path)
{
    BStringArray path_segments = BString__split(path, '/');
    BStringArray mount_segments = BString__split(mount_path, '/');
    BStringArray subpath_segments = EmptyArray(BStringArray);

    for (size_t i = BStringArray__len(&mount_segments); i < BStringArray__len(&path_segments); i++)
    {
        BString segment = EMPTY_BSTRING;
        match_optional(BString, BStringArray__at(&path_segments, i),
            some(path_segment) { segment = path_segment; },
            none() {}
        )
        BStringArray__push(&subpath_segments, segment);
    }

    return BString__join(&subpath_segments, '/');
}

static Optional(MountRoute) Filesystem__find_mount(FilesystemPtr self, BString path)
{
    Optional(MountRoute) best = None(MountRoute);
    size_t best_len = 0;

    MapIterate(Mounts, self->mounts, index, mount_path, mounted_fs)
        StringPos mount_path_length = BString__length(&mount_path);
        if (Filesystem__mount_matches(&path, &mount_path) && mount_path_length > (StringPos) best_len)
        {
            best_len = (size_t) mount_path_length;
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

Result(IOInternalHandle) RootFileIO__open(IOStatePtr self, BString path, Flags flags)
{
    FilesystemIOStatePtr fs_state = (FilesystemIOStatePtr) self;
    match_optional(MountRoute, Filesystem__find_mount(fs_state->filesystem, path),
        some(route)
        {
            __d_printf(FILESYSTEM, "mount found = %s", cstr(route.mount_path));
            return route.filesystem->file_io.base_io.open(
                &route.filesystem->state->state, route.subpath, flags
            );
        },
        none() { return Error(IOInternalHandle, BString__sprintf(&invalid_path_format, &path), IO_INVALID_PATH); }
    )

    return Error(IOInternalHandle, BString__sprintf(&invalid_path_format, &path), IO_INVALID_PATH);
}

Result(IOInternalHandle) RootFileIO__close(IOStatePtr self, IOInternalHandle handle)
{
    FilesystemIOStatePtr fs_state = (FilesystemIOStatePtr) self;
    return RootFileIO__delegate_close(fs_state, handle);
}

Result(IOBufferPtr) RootFileIO__read(
    IOStatePtr self, IOInternalHandle handle, size_t size, ExternalBuffer target_buffer
)
{
    FilesystemIOStatePtr fs_state = (FilesystemIOStatePtr) self;
    FilesystemCode code = (handle & FS_CODE_MASK);
    MapIterate(Mounts, fs_state->filesystem->mounts, index, name, filesys)
        if (filesys->fs_code == code)
        {
            return filesys->file_io.base_io.read(&filesys->state->state, handle, size, target_buffer);
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
    return Error(ParkStatus, not_implemented, IO_OPERATION_NOT_IMPLEMENTED);
}

Result(ParkStatus) RootFileIO__unpark(IOStatePtr self)
{
    return Error(ParkStatus, not_implemented, IO_OPERATION_NOT_IMPLEMENTED);
}

static DirectoryEntry RootFileIO__directory_entry_from_node(FilesystemNodePtr node)
{
    return DirectoryEntry__new(
        node->attribs,
        node->user_id,
        node->group_id,
        node->mode,
        &node->name
    );
}

static FilesystemNodePtr RootFileIO__node_from_directory_entry(FilesystemPtr filesystem, DirectoryEntryPtr entry)
{
    return FilesystemNode__heap_new_with_metadata(
        filesystem,
        bstring(entry->name),
        entry->attributes,
        entry->user_id,
        entry->group_id,
        entry->mode
    );
}

static BString RootFileIO__trim_path_separators(BString path)
{
    StringPos start = 0;
    StringPos end = BString__length(&path);

    while (start < end && BString__char_at_or(&path, start, '\0') == '/')
    {
        start++;
    }

    while (end > start && BString__char_at_or(&path, end - 1, '\0') == '/')
    {
        end--;
    }

    return BString__slice(&path, start, end);
}

static Optional(BString) RootFileIO__immediate_mount_child(BString path, BString mount_path)
{
    BString trimmed_path = RootFileIO__trim_path_separators(path);
    BString trimmed_mount_path = RootFileIO__trim_path_separators(mount_path);

    if (BString__is_empty(&trimmed_mount_path))
    {
        return None(BString);
    }

    if (BString__is_empty(&trimmed_path))
    {
        for (StringPos i = 0; i < BString__length(&trimmed_mount_path); i++)
        {
            if (BString__char_at_or(&trimmed_mount_path, i, '\0') == '/')
            {
                return Some(BString, BString__slice(&trimmed_mount_path, 0, i));
            }
        }

        return Some(BString, trimmed_mount_path);
    }

    if (!BString__starts_with(&trimmed_mount_path, &trimmed_path))
    {
        return None(BString);
    }

    if (
        BString__length(&trimmed_mount_path) <= BString__length(&trimmed_path)
        || BString__char_at_or(&trimmed_mount_path, BString__length(&trimmed_path), '\0') != '/'
    )
    {
        return None(BString);
    }

    StringPos child_start = BString__length(&trimmed_path) + 1;
    StringPos child_end = BString__length(&trimmed_mount_path);
    for (StringPos i = child_start; i < BString__length(&trimmed_mount_path); i++)
    {
        if (BString__char_at_or(&trimmed_mount_path, i, '\0') == '/')
        {
            child_end = i;
            break;
        }
    }

    return Some(BString, BString__slice(&trimmed_mount_path, child_start, child_end));
}

static bool RootFileIO__trace_list_dir(BStringPtr path)
{
    return BString__equals(path, &root_path);
}

static bool RootFileIO__entries_contain_name(
    DirectoryEntryPtr entries, DirectoryEntryCount count, const char * name
)
{
    for (DirectoryEntryCount i = 0; i < count; i++)
    {
        bool matches = true;
        for (size_t j = 0; name[j] != '\0' || entries[i].name[j] != '\0'; j++)
        {
            if (entries[i].name[j] != name[j])
            {
                matches = false;
                break;
            }
        }

        if (matches)
        {
            return true;
        }
    }

    return false;
}

static DirectoryEntry RootFileIO__directory_entry_from_mount(BString name)
{
    DirectoryEntry entry = (DirectoryEntry) {
        .attributes = 0,
        .user_id = PROCESS_USER_ROOT,
        .group_id = PROCESS_GROUP_ROOT,
        .mode = IO_MODE_TYPE_DIR
            | IO_MODE_OWNER_READ | IO_MODE_OWNER_EXEC
            | IO_MODE_GROUP_READ | IO_MODE_GROUP_EXEC
            | IO_MODE_OTHER_READ | IO_MODE_OTHER_EXEC,
        .name_len = 0,
    };

    size_t copy_len = (size_t) BString__copy_to(&name, entry.name, IO_DIRENT_NAME_MAX);
    entry.name_len = copy_len;

    return entry;
}

static Result(DirectoryEntryCount) RootFileIO__append_mount_entries(
    FilesystemPtr filesystem,
    BString path,
    DirectoryEntryPtr entries,
    DirectoryEntryCount count,
    size_t max_entries
)
{
    MapIterate(Mounts, filesystem->mounts, index, mount_path, mount_fs)
        if (RootFileIO__trace_list_dir(&path))
        {
            __d_printf(FILESYSTEM, "list_dir mount[%i] path=%s mount=%s\n", (int) index, cstr(path), cstr(mount_path));
        }
        match_optional(BString, RootFileIO__immediate_mount_child(path, mount_path),
            some(child)
            {
                if (RootFileIO__trace_list_dir(&path))
                {
                    __d_printf(FILESYSTEM, "list_dir child match mount=%s child=%s\n", cstr(mount_path), cstr(child));
                }
                if (child.len == 0 || RootFileIO__entries_contain_name(entries, count, cstr(child)))
                {
                    if (RootFileIO__trace_list_dir(&path))
                    {
                        __d_printf(FILESYSTEM, "list_dir child skipped child=%s existing=%i\n", cstr(child), (int) count);
                    }
                    continue;
                }

                if ((size_t) count >= max_entries)
                {
                    return Success(DirectoryEntryCount, count);
                }

                entries[count] = RootFileIO__directory_entry_from_mount(child);
                if (RootFileIO__trace_list_dir(&path))
                {
                    __d_printf(FILESYSTEM, "list_dir appended mount child entry[%i]=%s\n", (int) count, entries[count].name);
                }
                count++;
            },
            none() {}
        )

    End
    return Success(DirectoryEntryCount, count);
}

static Result(Anything) RootFileIO__complete_async_list_dir(Result(Anything) result, Anything raw_context)
{
    RootAsyncListDirContextPtr context = ptr(raw_context, RootAsyncListDirContext);
    Result(Anything) mapped = result;

    match_result(Anything, result,
        success(value)
        {
            DirectoryEntryCount count = (DirectoryEntryCount) value;
            match_result(DirectoryEntryCount, RootFileIO__append_mount_entries(
                context->filesystem, context->path, context->entries, count, context->max_entries
            ),
                success(merged_count)
                {
                    mapped = Success(Anything, (Anything) merged_count);
                },
                error(code, message)
                {
                    mapped = Error(Anything, message, code);
                })
        },
        error(code, message)
        {
            match_result(DirectoryEntryCount, RootFileIO__append_mount_entries(
                context->filesystem, context->path, context->entries, 0, context->max_entries
            ),
                success(merged_count)
                {
                    if (merged_count > 0)
                    {
                        mapped = Success(Anything, (Anything) merged_count);
                    }
                    else
                    {
                        mapped = Error(Anything, message, code);
                    }
                },
                error(merge_code, merge_message)
                {
                    mapped = Error(Anything, merge_message, merge_code);
                })
        }
    )

    match_optional(KernelResultMapper, context->parent_mapper,
        some(parent_mapper)
        {
            Kernel__free(addr(context));
            return parent_mapper(mapped, context->parent_mapper_context);
        },
        none() {}
    )

    Kernel__free(addr(context));
    return mapped;
}

static bool RootFileIO__nodes_contain_name(FilesystemNodes * nodes, BStringPtr name)
{
    for (uint64_t index = 0; index < FilesystemNodes__len(nodes); index++)
    {
        match_optional(FilesystemNodePtr, FilesystemNodes__at(nodes, index),
            some(node)
            {
                if (BString__equals(&node->name, name))
                {
                    return true;
                }
            },
            none() {}
        )
    }

    return false;
}

static Result(CollectionCode) RootFileIO__append_node_if_missing(FilesystemNodes * nodes, FilesystemNodePtr node)
{
    if (RootFileIO__nodes_contain_name(nodes, &node->name))
    {
        return Success(CollectionCode, 0);
    }

    return FilesystemNodes__push(nodes, node);
}

static Result(CollectionCode) RootFileIO__append_nodes(FilesystemNodes * target, FilesystemNodes source)
{
    FAIterate(FilesystemNodes, source, index, node)
        match_result(CollectionCode, FilesystemNodes__push(target, node),
            success(_) {},
            error(code, message) { return Error(CollectionCode, message, code); }
        )

    End
    return Success(CollectionCode, 0);
}

static Result(FilesystemNodes) RootFileIO__nodes_from_directory_entries(
    FilesystemPtr filesystem, DirectoryEntryPtr entries, DirectoryEntryCount count
)
{
    FilesystemNodes nodes = EmptyArray(FilesystemNodes);

    for (DirectoryEntryCount i = 0; i < count; i++)
    {
        match_result(CollectionCode, FilesystemNodes__push(
            &nodes, RootFileIO__node_from_directory_entry(filesystem, &entries[i])
        ),
            success(_) {},
            error(code, message) { return Error(FilesystemNodes, message, code); }
        )
    }

    return Success(FilesystemNodes, nodes);
}

static Result(CollectionCode) RootFileIO__append_mount_children(
    FilesystemPtr filesystem, BString path, FilesystemNodes * result
)
{
    MapIterate(Mounts, filesystem->mounts, index, mount_path, mount_fs)
        match_optional(BString, RootFileIO__immediate_mount_child(path, mount_path),
            some(child)
            {
                match_result(CollectionCode, RootFileIO__append_node_if_missing(
                    result, FilesystemNode__heap_new(mount_fs, child, 0)
                ),
                    success(_) {},
                    error(code, message) { return Error(CollectionCode, message, code); }
                )
            },
            none() {}
        )

    End
    return Success(CollectionCode, 0);
}

static Result(DirectoryEntryCount) RootFileIO__copy_nodes_to_entries(
    FilesystemNodes nodes, DirectoryEntryPtr entries, size_t max_entries
)
{
    DirectoryEntryCount count = 0;
    FAIterate(FilesystemNodes, nodes, index, node)
        if ((size_t) count >= max_entries)
        {
            return Success(DirectoryEntryCount, count);
        }
        entries[count] = RootFileIO__directory_entry_from_node(node);
        count++;

    End
    return Success(DirectoryEntryCount, count);
}

Result(FilesystemNodes) RootFileIO__list_nodes(IOStatePtr self, FilesystemNodePtr parent, BString path)
{
    FilesystemNodes result = EmptyArray(FilesystemNodes);
    FilesystemIOStatePtr fs_state = (FilesystemIOStatePtr) self;
    FilesystemPtr filesystem = fs_state->filesystem;
    bool listed_backing = false;

    match_optional(MountRoute, Filesystem__find_mount(filesystem, path),
        some(route)
        {
            if (route.filesystem->file_io.list_nodes != NULL) // hundelint: ignore-no-null
            {
                match_result(FilesystemNodes, route.filesystem->file_io.list_nodes(
                    &route.filesystem->state->state, filesystem->root, route.subpath
                ),
                    success(nodes)
                    {
                        listed_backing = true;
                        match_result(CollectionCode, RootFileIO__append_nodes(&result, nodes),
                            success(_) {},
                            error(code, message) { return Error(FilesystemNodes, message, code); }
                        )
                    }, error(code, message) {})
            }
            else if (route.filesystem->file_io.list_dir != NULL) // hundelint: ignore-no-null
            {
                DirectoryEntry entries[MAX_CHILD_NODES] = { 0 };
                match_result(DirectoryEntryCount, route.filesystem->file_io.list_dir(
                    &route.filesystem->state->state, route.subpath, entries, MAX_CHILD_NODES
                ),
                    success(count)
                    {
                        listed_backing = true;
                        match_result(FilesystemNodes, RootFileIO__nodes_from_directory_entries(
                            route.filesystem, entries, count
                        ),
                            success(nodes)
                            {
                                match_result(CollectionCode, RootFileIO__append_nodes(&result, nodes),
                                    success(_) {},
                                    error(code, message) { return Error(FilesystemNodes, message, code); }
                                )
                            }, error(code, message) { return Error(FilesystemNodes, message, code); })
                    }, error(code, message) {})
            }
        },
        none() {}
    )

    match_result(CollectionCode, RootFileIO__append_mount_children(filesystem, path, &result),
        success(_) {},
        error(code, message) { return Error(FilesystemNodes, message, code); }
    )

    if (listed_backing || FilesystemNodes__len(&result) > 0)
    {
        return Success(FilesystemNodes, result);
    }

    return Error(FilesystemNodes, BString__sprintf(&invalid_path_format, &path), IO_INVALID_PATH);
}

Result(DirectoryEntryCount) RootFileIO__list_dir(
    IOStatePtr self, BString path, DirectoryEntryPtr entries, size_t max_entries
)
{
    FilesystemIOStatePtr fs_state = (FilesystemIOStatePtr) self;
    DirectoryEntryCount count = 0;
    bool listed_backing = false;
    bool backing_request_pending = false;
    BString backing_pending_message = EMPTY_BSTRING;

    if (RootFileIO__trace_list_dir(&path))
    {
        __d_printf(FILESYSTEM, "list_dir start path=%s mounts=%i max=%i\n",
            cstr(path), (int) fs_state->filesystem->mounts.len, (int) max_entries);
    }

    match_optional(MountRoute, Filesystem__find_mount(fs_state->filesystem, path),
        some(route)
        {
            if (RootFileIO__trace_list_dir(&path))
            {
                __d_printf(FILESYSTEM, "list_dir route mount=%s subpath=%s fs=%s\n",
                    cstr(route.mount_path), cstr(route.subpath), cstr(route.filesystem->name));
            }
            if (route.filesystem->file_io.list_dir != NULL) // hundelint: ignore-no-null
            {
                RootAsyncListDirContextPtr async_context = 0;
                Optional(KernelContextPtr) current_context = BlockDevice__current_context();
                KernelContext wrapped_context = EMPTY_KERNEL_CONTEXT;

                match_optional(KernelContextPtr, current_context,
                    some(existing_context)
                    {
                        async_context = ptr(Kernel__alloc_kernel(sizeof(RootAsyncListDirContext)), RootAsyncListDirContext);
                        async_context->filesystem = fs_state->filesystem;
                        async_context->path = path;
                        async_context->entries = entries;
                        async_context->max_entries = max_entries;
                        async_context->parent_mapper = existing_context->result_mapper;
                        async_context->parent_mapper_context = existing_context->result_mapper_context;
                        wrapped_context = *existing_context;
                        wrapped_context.result_mapper = Some(KernelResultMapper, RootFileIO__complete_async_list_dir);
                        wrapped_context.result_mapper_context = (Anything) async_context;
                        BlockDevice__set_current_context(&wrapped_context);
                    },
                    none() {}
                )

                match_result(DirectoryEntryCount, route.filesystem->file_io.list_dir(
                    &route.filesystem->state->state, route.subpath, entries, max_entries
                ),
                    success(backing_count)
                    {
                        if (async_context != 0)
                        {
                            Kernel__free(addr(async_context));
                        }
                        count = backing_count;
                        listed_backing = true;
                        if (RootFileIO__trace_list_dir(&path))
                        {
                            __d_printf(FILESYSTEM, "list_dir backing count=%i\n", (int) backing_count);
                            for (DirectoryEntryCount i = 0; i < backing_count; i++)
                            {
                                __d_printf(FILESYSTEM, "list_dir backing entry[%i]=%s\n", (int) i, entries[i].name);
                            }
                        }
                    },
                    error(code, message)
                    {
                        if (code == BLOCK_DEVICE_REQUEST_PENDING)
                        {
                            backing_request_pending = true;
                            backing_pending_message = message;
                        }
                        if (code != BLOCK_DEVICE_REQUEST_PENDING && async_context != 0)
                        {
                            Kernel__free(addr(async_context));
                        }
                        if (RootFileIO__trace_list_dir(&path))
                        {
                            __d_printf(FILESYSTEM, "list_dir backing error code=%i message=%s\n", (int) code, cstr(message));
                        }
                    })

                match_optional(KernelContextPtr, current_context,
                    some(existing_context) { BlockDevice__set_current_context(existing_context); },
                    none() { BlockDevice__clear_current_context(); }
                )

                if (backing_request_pending)
                {
                    return Error(DirectoryEntryCount, backing_pending_message, BLOCK_DEVICE_REQUEST_PENDING);
                }
            }
            else if (route.filesystem->file_io.list_nodes != NULL) // hundelint: ignore-no-null
            {
                match_result(FilesystemNodes, route.filesystem->file_io.list_nodes(
                    &route.filesystem->state->state, fs_state->filesystem->root, route.subpath
                ),
                    success(nodes)
                    {
                        listed_backing = true;
                        match_result(DirectoryEntryCount, RootFileIO__copy_nodes_to_entries(nodes, entries, max_entries),
                            success(backing_count) { count = backing_count; },
                            error(code, message) { return Error(DirectoryEntryCount, message, code); }
                        )
                    }, error(code, message) {})
            }
        },
        none() {}
    )

    match_result(DirectoryEntryCount, RootFileIO__append_mount_entries(
        fs_state->filesystem, path, entries, count, max_entries
    ),
        success(merged_count)
        {
            if (RootFileIO__trace_list_dir(&path))
            {
                __d_printf(FILESYSTEM, "list_dir merged count=%i listed_backing=%i\n",
                    (int) merged_count, (int) listed_backing);
                for (DirectoryEntryCount i = 0; i < merged_count; i++)
                {
                    __d_printf(FILESYSTEM, "list_dir final entry[%i]=%s\n", (int) i, entries[i].name);
                }
            }
            if (listed_backing || merged_count > 0)
            {
                return Success(DirectoryEntryCount, merged_count);
            }
        }, error(code, message) { return Error(DirectoryEntryCount, message, code); })

    return Error(DirectoryEntryCount, BString__sprintf(&invalid_path_format, &path), IO_INVALID_PATH);
}

Result(IOStatus) RootFileIO__mkdir(IOStatePtr self, BString path)
{
    FilesystemIOStatePtr fs_state = (FilesystemIOStatePtr) self;
    match_optional(MountRoute, Filesystem__find_mount(fs_state->filesystem, path),
        some(route) { return route.filesystem->file_io.mkdir(&route.filesystem->state->state, route.subpath); },
        none() { return Error(IOStatus, BString__sprintf(&invalid_path_format, &path), IO_INVALID_PATH); }
    )

    return Error(IOStatus, BString__sprintf(&invalid_path_format, &path), IO_INVALID_PATH);
}

Result(IOStatus) RootFileIO__remove(IOStatePtr self, BString path, Flags flags)
{
    FilesystemIOStatePtr fs_state = (FilesystemIOStatePtr) self;
    match_optional(MountRoute, Filesystem__find_mount(fs_state->filesystem, path),
        some(route) { return route.filesystem->file_io.remove(&route.filesystem->state->state, route.subpath, flags); },
        none() { return Error(IOStatus, BString__sprintf(&invalid_path_format, &path), IO_INVALID_PATH); }
    )

    return Error(IOStatus, BString__sprintf(&invalid_path_format, &path), IO_INVALID_PATH);
}

Result(IOStatus) RootFileIO__link(IOStatePtr self, BString source_path, BString target_path)
{
    FilesystemIOStatePtr fs_state = (FilesystemIOStatePtr) self;
    match_optional(MountRoute, Filesystem__find_mount(fs_state->filesystem, source_path),
        some(source_route)
        {
            match_optional(MountRoute, Filesystem__find_mount(fs_state->filesystem, target_path),
                some(target_route)
                {
                    if (source_route.filesystem != target_route.filesystem || source_route.filesystem->state != target_route.filesystem->state)
                    {
                        return Error(IOStatus, invalid_path_format, IO_OPERATION_NOT_SUPPORTED);
                    }

                    return source_route.filesystem->file_io.link(
                        &source_route.filesystem->state->state,
                        source_route.subpath,
                        target_route.subpath
                    );
                },
                none() { return Error(IOStatus, BString__sprintf(&invalid_path_format, &target_path), IO_INVALID_PATH); }
            )
        },
        none() { return Error(IOStatus, BString__sprintf(&invalid_path_format, &source_path), IO_INVALID_PATH); }
    )

    return Error(IOStatus, BString__sprintf(&invalid_path_format, &source_path), IO_INVALID_PATH);
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
        .list_dir = RootFileIO__list_dir,
        .mkdir = RootFileIO__mkdir,
        .remove = RootFileIO__remove,
        .link = RootFileIO__link,
    };

    return root_file_io;
}

static FilesystemCode fs_codes = 0x01;


FilesystemPtr Filesystem__heap_new(BString name, FileIO file_io)
{
    FilesystemPtr fs = ptr(Kernel__alloc_kernel(sizeof(Filesystem)), Filesystem);
    FilesystemCode code = fs_codes++;

    *fs = (Filesystem) {
        .name = name,
        .mounts = Mounts__new(),
        .filesystem_table = FilesystemTable__new(),
        .file_io = file_io,
        .fs_code = (code << CODE_SHIFT),
    };
    __d_printf(FILESYSTEM, "Created filesystem %x %p with code %x", sizeof(Filesystem), fs, code);
    return fs;
}

FilesystemPtr RootFilesystem__heap_new()
{
    return Filesystem__heap_new(bstring("root"), RootFilesystemIO__new());
}

Result(MountResult) Filesystem__mount(FilesystemPtr self, BString path, FilesystemPtr filesystem)
{
    Mounts__set(&self->mounts, path, filesystem);

    return Success(MountResult, MOUNT_OK);
}

FileHandleTargetPtr FileHandleTarget__heap_new(BString path)
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
