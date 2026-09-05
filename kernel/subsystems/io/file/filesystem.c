#include "filesystem.h"
#include "../../../kalloc.h"
#include "../block/block_device.h"

MapBodyDefine(Mounts);
MapBodyDefine(FileHandleTargetMap);
LinkedListBodyDefine(FilesystemTableEntries);

static BString invalid_path_format = bstr("Invalid path %B");
static BString not_implemented = bstr("Operation not implemented");
static BString file_operation_not_supported = bstr("File operation not supported");
static BString invalid_handle = bstr("Unknown handle");
static BString root_path = bstr("/");

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

FilesystemTableEntry FilesystemTableEntry__new(
    ConstBStringPtr device_path,
    ConstBStringPtr filesystem_type,
    ConstBStringPtr mountpoint,
    bool match_block_count,
    uint64_t block_count
)
{
    return (FilesystemTableEntry) {
        .device_path = *device_path,
        .filesystem_type = *filesystem_type,
        .mountpoint = *mountpoint,
        .match_block_count = match_block_count,
        .block_count = block_count,
    };
}

FilesystemTable FilesystemTable__new()
{
    return (FilesystemTable) {
        .entries = FilesystemTableEntries__new(),
    };
}

Result(CollectionCode) FilesystemTable__add(
    FilesystemTablePtr self, ConstBStringPtr device_path, ConstBStringPtr filesystem_type, ConstBStringPtr mountpoint
)
{
    return FilesystemTableEntries__push(
        &self->entries,
        FilesystemTableEntry__new(device_path, filesystem_type, mountpoint, false, 0)
    );
}

Result(CollectionCode) FilesystemTable__add_by_block_count(
    FilesystemTablePtr self, uint64_t block_count, ConstBStringPtr filesystem_type, ConstBStringPtr mountpoint
)
{
    BString empty_path = empty_bstr();
    return FilesystemTableEntries__push(
        &self->entries,
        FilesystemTableEntry__new(&empty_path, filesystem_type, mountpoint, true, block_count)
    );
}

typedef struct MountRoute
{
    FilesystemPtr filesystem;
    BString mount_path;
    BString subpath;
}
MountRoute;

OptionalDef(MountRoute);

Result(FilesystemNodes) FileIO__unsupported_list_nodes(IOStatePtr self, FilesystemNodePtr parent, ConstBStringPtr path)
{
    return Error(FilesystemNodes, file_operation_not_supported, IO_OPERATION_NOT_SUPPORTED);
}

Result(DirectoryEntryCount) FileIO__unsupported_list_dir(
    IOStatePtr self, ConstBStringPtr path, DirectoryEntryPtr entries, size_t max_entries
)
{
    return Error(DirectoryEntryCount, file_operation_not_supported, IO_OPERATION_NOT_SUPPORTED);
}

Result(IOStatus) FileIO__unsupported_mkdir(IOStatePtr self, ConstBStringPtr path)
{
    return Error(IOStatus, file_operation_not_supported, IO_OPERATION_NOT_SUPPORTED);
}

Result(IOStatus) FileIO__unsupported_remove(IOStatePtr self, ConstBStringPtr path, Flags flags)
{
    return Error(IOStatus, file_operation_not_supported, IO_OPERATION_NOT_SUPPORTED);
}

Result(IOStatus) FileIO__unsupported_link(IOStatePtr self, ConstBStringPtr source_path, ConstBStringPtr target_path)
{
    return Error(IOStatus, file_operation_not_supported, IO_OPERATION_NOT_SUPPORTED);
}

static MountRoute MountRoute__new(FilesystemPtr filesystem, ConstBStringPtr mount_path, ConstBStringPtr subpath)
{
    return (MountRoute) {
        .filesystem = filesystem,
        .mount_path = *mount_path,
        .subpath = *subpath,
    };
}

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

OptionalDef(RootAsyncListDirContextPtr);

static Result(Anything) RootFileIO__complete_async_list_dir(ResultPtr(Anything) result, Anything raw_context);

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
        BString path_segment = empty_bstr();
        BString mount_segment = empty_bstr();
        match_optional(BString, BStringArray__at(&path_segments, i),
            some(segment) { path_segment = segment; },
            none() { return false; }
        );
        match_optional(BString, BStringArray__at(&mount_segments, i),
            some(segment) { mount_segment = segment; },
            none() { return false; }
        );
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
        BString segment = empty_bstr();
        match_optional(BString, BStringArray__at(&path_segments, i),
            some(path_segment) { segment = path_segment; },
            none_do_nothing()
        );
        BStringArray__push(&subpath_segments, segment);
    }

    return BString__join(&subpath_segments, '/');
}

static Optional(MountRoute) Filesystem__find_mount(FilesystemPtr self, ConstBStringPtr path)
{
    Optional(MountRoute) best = None(MountRoute);
    size_t best_len = 0;

    for_each_map(Mounts, self->mounts, index, mount_path, mounted_fs,
    {
        StringPos mount_path_length = BString__length(&mount_path);
        if (Filesystem__mount_matches((BStringPtr) path, &mount_path) && mount_path_length > (StringPos) best_len)
        {
            best_len = (size_t) mount_path_length;
            BString subpath = Filesystem__subpath((BStringPtr) path, &mount_path);
            best = Some(MountRoute, MountRoute__new(mounted_fs, &mount_path, &subpath));
        }

    });
    return best;
}

static Result(IOInternalHandle) RootFileIO__delegate_close(FilesystemIOStatePtr self, IOInternalHandle handle)
{
    FilesystemCode code = (handle & FS_CODE_MASK);
    for_each_map(Mounts, self->filesystem->mounts, index, name, filesys,
    {
        if (filesys->fs_code == code)
        {
            return filesys->file_io.base_io.close(&filesys->state->state, handle);
        }

    });
    return Error(IOInternalHandle, invalid_handle, IO_UNKNOWN_HANDLE);
}

static Result(ByteCount) RootFileIO__delegate_write(
    FilesystemIOStatePtr self, IOInternalHandle handle, DataPtr data, size_t size
)
{
    FilesystemCode code = (handle & FS_CODE_MASK);
    for_each_map(Mounts, self->filesystem->mounts, index, name, filesys,
    {
        if (filesys->fs_code == code)
        {
            return filesys->file_io.base_io.write(&filesys->state->state, handle, data, size);
        }

    });
    return Error(ByteCount, invalid_handle, IO_UNKNOWN_HANDLE);
}

Result(IOInternalHandle) RootFileIO__open(IOStatePtr self, ConstBStringPtr path, Flags flags)
{
    FilesystemIOStatePtr fs_state = (FilesystemIOStatePtr) self;
    match_optional(MountRoute, Filesystem__find_mount(fs_state->filesystem, path),
        some(route)
        {
            __d_printf(FILESYSTEM, "mount found = %s", cstr(route.mount_path));
            return route.filesystem->file_io.base_io.open(
                &route.filesystem->state->state, &route.subpath, flags
            );
        },
        none() { return Error(IOInternalHandle, BString__sprintf(&invalid_path_format, path), IO_INVALID_PATH); }
    );

    return Error(IOInternalHandle, BString__sprintf(&invalid_path_format, path), IO_INVALID_PATH);
}

Result(IOInternalHandle) RootFileIO__close(IOStatePtr self, IOInternalHandle handle)
{
    FilesystemIOStatePtr fs_state = (FilesystemIOStatePtr) self;
    return RootFileIO__delegate_close(fs_state, handle);
}

Result(IOBufferPtr) RootFileIO__read(
    IOStatePtr self, IOInternalHandle handle, size_t size, ConstExternalBufferPtr target_buffer
)
{
    FilesystemIOStatePtr fs_state = (FilesystemIOStatePtr) self;
    FilesystemCode code = (handle & FS_CODE_MASK);
    for_each_map(Mounts, fs_state->filesystem->mounts, index, name, filesys,
    {
        if (filesys->fs_code == code)
        {
            return filesys->file_io.base_io.read(&filesys->state->state, handle, size, target_buffer);
        }

    });
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
    BString name = bstring(entry->name);
    return FilesystemNode__heap_new_with_metadata(
        filesystem,
        &bstr("root"),
        entry->attributes,
        entry->user_id,
        entry->group_id,
        entry->mode
    );
}

static BString RootFileIO__trim_path_separators(ConstBStringPtr path)
{
    StringPos start = 0;
    StringPos end = BString__length(path);

    while (start < end && BString__char_at_or(path, start, '\0') == '/')
    {
        start++;
    }

    while (end > start && BString__char_at_or(path, end - 1, '\0') == '/')
    {
        end--;
    }

    return BString__slice(path, start, end);
}

static Optional(BString) RootFileIO__immediate_mount_child(ConstBStringPtr path, ConstBStringPtr mount_path)
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

static DirectoryEntry RootFileIO__directory_entry_from_mount(ConstBStringPtr name)
{
    return DirectoryEntry__new(
        0,
        PROCESS_USER_ROOT,
        PROCESS_GROUP_ROOT,
        IO_MODE_TYPE_DIR
            | IO_MODE_OWNER_READ | IO_MODE_OWNER_EXEC
            | IO_MODE_GROUP_READ | IO_MODE_GROUP_EXEC
            | IO_MODE_OTHER_READ | IO_MODE_OTHER_EXEC,
        name
    );
}

static Result(DirectoryEntryCount) RootFileIO__append_mount_entries(
    FilesystemPtr filesystem,
    ConstBStringPtr path,
    DirectoryEntryPtr entries,
    DirectoryEntryCount count,
    size_t max_entries
)
{
    for_each_map(Mounts, filesystem->mounts, index, mount_path, mount_fs,
    {
        if (RootFileIO__trace_list_dir((BStringPtr) path))
        {
            __d_printf(FILESYSTEM, "list_dir mount[%i] path=%s mount=%s\n", (int) index, cstr_p(path), cstr(mount_path));
        }
        match_optional(BString, RootFileIO__immediate_mount_child(path, &mount_path),
            some(child)
            {
                if (RootFileIO__trace_list_dir((BStringPtr) path))
                {
                    __d_printf(FILESYSTEM, "list_dir child match mount=%s child=%s\n", cstr(mount_path), cstr(child));
                }
                if (child.len == 0 || RootFileIO__entries_contain_name(entries, count, cstr(child)))
                {
                    if (RootFileIO__trace_list_dir((BStringPtr) path))
                    {
                        __d_printf(FILESYSTEM, "list_dir child skipped child=%s existing=%i\n", cstr(child), (int) count);
                    }
                    continue;
                }

                if ((size_t) count >= max_entries)
                {
                    return Success(DirectoryEntryCount, count);
                }

                entries[count] = RootFileIO__directory_entry_from_mount(&child);
                if (RootFileIO__trace_list_dir((BStringPtr) path))
                {
                    __d_printf(FILESYSTEM, "list_dir appended mount child entry[%i]=%s\n", (int) count, entries[count].name);
                }
                count++;
            },
            none_do_nothing()
        );

    });
    return Success(DirectoryEntryCount, count);
}

static Result(Anything) RootFileIO__complete_async_list_dir(ResultPtr(Anything) result, Anything raw_context)
{
    RootAsyncListDirContextPtr context = ptr(raw_context, RootAsyncListDirContext);
    Result(Anything) mapped = *result;

    match_result(Anything, *result,
        success(value)
        {
            DirectoryEntryCount count = (DirectoryEntryCount) value;
            match_result(DirectoryEntryCount, RootFileIO__append_mount_entries(
                context->filesystem, &context->path, context->entries, count, context->max_entries
            ),
                success(merged_count)
                {
                    mapped = Success(Anything, (Anything) merged_count);
                },
                error(code, message)
                {
                    mapped = Error(Anything, message, code);
                });
        },
        error(code, message)
        {
            match_result(DirectoryEntryCount, RootFileIO__append_mount_entries(
                context->filesystem, &context->path, context->entries, 0, context->max_entries
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
                });
        }
    );

    match_optional(KernelResultMapper, context->parent_mapper,
        some(parent_mapper)
        {
            Kernel__free(addr(context));
            return parent_mapper(&mapped, context->parent_mapper_context);
        },
        none_do_nothing()
    );

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
            none_do_nothing()
        );
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
    for_each_fa(FilesystemNodes, source, index, node,
    {
        match_result(CollectionCode, FilesystemNodes__push(target, node),
            success_do_nothing(),
            error(code, message) { return Error(CollectionCode, message, code); }
        );

    });
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
            success_do_nothing(),
            error(code, message) { return Error(FilesystemNodes, message, code); }
        );
    }

    return Success(FilesystemNodes, nodes);
}

static Result(CollectionCode) RootFileIO__append_mount_children(
    FilesystemPtr filesystem, ConstBStringPtr path, FilesystemNodes * result
)
{
    for_each_map(Mounts, filesystem->mounts, index, mount_path, mount_fs,
    {
        match_optional(BString, RootFileIO__immediate_mount_child(path, &mount_path),
            some(child)
            {
                match_result(CollectionCode, RootFileIO__append_node_if_missing(
                    result, FilesystemNode__heap_new(mount_fs, &child, 0)
                ),
                    success_do_nothing(),
                    error(code, message) { return Error(CollectionCode, message, code); }
                );
            },
            none_do_nothing()
        );

    });
    return Success(CollectionCode, 0);
}

static Result(DirectoryEntryCount) RootFileIO__copy_nodes_to_entries(
    const FilesystemNodes * nodes, DirectoryEntryPtr entries, size_t max_entries
)
{
    DirectoryEntryCount count = 0;
    for_each_fa_ptr(FilesystemNodes, nodes, index, node,
    {
        if ((size_t) count >= max_entries)
        {
            return Success(DirectoryEntryCount, count);
        }
        entries[count] = RootFileIO__directory_entry_from_node(node);
        count++;

    });
    return Success(DirectoryEntryCount, count);
}

Result(FilesystemNodes) RootFileIO__list_nodes(IOStatePtr self, FilesystemNodePtr parent, ConstBStringPtr path)
{
    FilesystemNodes result = EmptyArray(FilesystemNodes);
    FilesystemIOStatePtr fs_state = (FilesystemIOStatePtr) self;
    FilesystemPtr filesystem = fs_state->filesystem;
    bool listed_backing = false;

    match_optional(MountRoute, Filesystem__find_mount(filesystem, path),
        some(route)
        {
            bool list_nodes_unsupported = false;
            match_result(FilesystemNodes, route.filesystem->file_io.list_nodes(
                &route.filesystem->state->state, filesystem->root, &route.subpath
            ),
                success(nodes)
                {
                    listed_backing = true;
                    match_result(CollectionCode, RootFileIO__append_nodes(&result, nodes),
                        success_do_nothing(),
                        error(code, message) { return Error(FilesystemNodes, message, code); }
                    );
                },
                error(code, message)
                {
                    listed_backing = false;
                    if (code == IO_OPERATION_NOT_SUPPORTED)
                    {
                        list_nodes_unsupported = true;
                    }
                });
            if (list_nodes_unsupported)
            {
                DirectoryEntry entries[MAX_CHILD_NODES] = { 0 };
                match_result(DirectoryEntryCount, route.filesystem->file_io.list_dir(
                    &route.filesystem->state->state, &route.subpath, entries, MAX_CHILD_NODES
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
                                    success_do_nothing(),
                                    error(code, message) { return Error(FilesystemNodes, message, code); }
                                );
                            }, error(code, message) { return Error(FilesystemNodes, message, code); });
                    }, error(code, message) { listed_backing = false; });
            }
        },
        none_do_nothing()
    );

    match_result(CollectionCode, RootFileIO__append_mount_children(filesystem, path, &result),
        success_do_nothing(),
        error(code, message) { return Error(FilesystemNodes, message, code); }
    );

    if (listed_backing || FilesystemNodes__len(&result) > 0)
    {
        return Success(FilesystemNodes, result);
    }

    return Error(FilesystemNodes, BString__sprintf(&invalid_path_format, path), IO_INVALID_PATH);
}

Result(DirectoryEntryCount) RootFileIO__list_dir(
    IOStatePtr self, ConstBStringPtr path, DirectoryEntryPtr entries, size_t max_entries
)
{
    FilesystemIOStatePtr fs_state = (FilesystemIOStatePtr) self;
    DirectoryEntryCount count = 0;
    bool listed_backing = false;
    bool backing_request_pending = false;
    BString backing_pending_message = empty_bstr();

    if (RootFileIO__trace_list_dir((BStringPtr) path))
    {
        __d_printf(FILESYSTEM, "list_dir start path=%s mounts=%i max=%i\n",
            cstr_p(path), (int) fs_state->filesystem->mounts.len, (int) max_entries);
    }

    match_optional(MountRoute, Filesystem__find_mount(fs_state->filesystem, path),
        some(route)
        {
            if (RootFileIO__trace_list_dir((BStringPtr) path))
            {
                __d_printf(FILESYSTEM, "list_dir route mount=%s subpath=%s fs=%s\n",
                    cstr(route.mount_path), cstr(route.subpath), cstr(route.filesystem->name));
            }
            bool list_dir_unsupported = false;
            Optional(RootAsyncListDirContextPtr) async_context = None(RootAsyncListDirContextPtr);
            Optional(KernelContextPtr) current_context = BlockDevice__current_context();
            KernelContext wrapped_context = EMPTY_KERNEL_CONTEXT;

            match_optional(KernelContextPtr, current_context,
                some(existing_context)
                {
                    RootAsyncListDirContextPtr allocated_context =
                        ptr(Kernel__alloc_kernel(sizeof(RootAsyncListDirContext)), RootAsyncListDirContext);
                    allocated_context->filesystem = fs_state->filesystem;
                    allocated_context->path = *path;
                    allocated_context->entries = entries;
                    allocated_context->max_entries = max_entries;
                    allocated_context->parent_mapper = existing_context->result_mapper;
                    allocated_context->parent_mapper_context = existing_context->result_mapper_context;
                    wrapped_context = *existing_context;
                    wrapped_context.result_mapper = Some(KernelResultMapper, RootFileIO__complete_async_list_dir);
                    wrapped_context.result_mapper_context = (Anything) allocated_context;
                    async_context = Some(RootAsyncListDirContextPtr, allocated_context);
                    BlockDevice__set_current_context(&wrapped_context);
                },
                none_do_nothing()
            );

            match_result(DirectoryEntryCount, route.filesystem->file_io.list_dir(
                &route.filesystem->state->state, &route.subpath, entries, max_entries
            ),
                success(backing_count)
                {
                    match_optional(RootAsyncListDirContextPtr, async_context,
                        some(context) { Kernel__free(addr(context)); },
                        none_do_nothing()
                    );
                    count = backing_count;
                    listed_backing = true;
                    if (RootFileIO__trace_list_dir((BStringPtr) path))
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
                    if (code != BLOCK_DEVICE_REQUEST_PENDING)
                    {
                        match_optional(RootAsyncListDirContextPtr, async_context,
                            some(context) { Kernel__free(addr(context)); },
                            none_do_nothing()
                        );
                    }
                    if (code == IO_OPERATION_NOT_SUPPORTED)
                    {
                        list_dir_unsupported = true;
                    }
                    if (RootFileIO__trace_list_dir((BStringPtr) path))
                    {
                        __d_printf(FILESYSTEM, "list_dir backing error code=%i message=%s\n", (int) code, cstr(message));
                    }
                });

            match_optional(KernelContextPtr, current_context,
                some(existing_context) { BlockDevice__set_current_context(existing_context); },
                none() { BlockDevice__clear_current_context(); }
            );

            if (backing_request_pending)
            {
                return Error(DirectoryEntryCount, backing_pending_message, BLOCK_DEVICE_REQUEST_PENDING);
            }

            if (list_dir_unsupported)
            {
                match_result(FilesystemNodes, route.filesystem->file_io.list_nodes(
                    &route.filesystem->state->state, fs_state->filesystem->root, &route.subpath
                ),
                    success(nodes)
                    {
                        listed_backing = true;
                        match_result(DirectoryEntryCount, RootFileIO__copy_nodes_to_entries(&nodes, entries, max_entries),
                            success(backing_count) { count = backing_count; },
                            error(code, message) { return Error(DirectoryEntryCount, message, code); }
                        );
                    }, error(code, message) { listed_backing = false; });
            }
        },
        none_do_nothing()
    );

    match_result(DirectoryEntryCount, RootFileIO__append_mount_entries(
        fs_state->filesystem, path, entries, count, max_entries
    ),
        success(merged_count)
        {
            if (RootFileIO__trace_list_dir((BStringPtr) path))
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
        }, error(code, message) { return Error(DirectoryEntryCount, message, code); });

    return Error(DirectoryEntryCount, BString__sprintf(&invalid_path_format, path), IO_INVALID_PATH);
}

Result(IOStatus) RootFileIO__mkdir(IOStatePtr self, ConstBStringPtr path)
{
    FilesystemIOStatePtr fs_state = (FilesystemIOStatePtr) self;
    match_optional(MountRoute, Filesystem__find_mount(fs_state->filesystem, path),
        some(route) { return route.filesystem->file_io.mkdir(&route.filesystem->state->state, &route.subpath); },
        none() { return Error(IOStatus, BString__sprintf(&invalid_path_format, path), IO_INVALID_PATH); }
    );

    return Error(IOStatus, BString__sprintf(&invalid_path_format, path), IO_INVALID_PATH);
}

Result(IOStatus) RootFileIO__remove(IOStatePtr self, ConstBStringPtr path, Flags flags)
{
    FilesystemIOStatePtr fs_state = (FilesystemIOStatePtr) self;
    match_optional(MountRoute, Filesystem__find_mount(fs_state->filesystem, path),
        some(route) { return route.filesystem->file_io.remove(&route.filesystem->state->state, &route.subpath, flags); },
        none() { return Error(IOStatus, BString__sprintf(&invalid_path_format, path), IO_INVALID_PATH); }
    );

    return Error(IOStatus, BString__sprintf(&invalid_path_format, path), IO_INVALID_PATH);
}

Result(IOStatus) RootFileIO__link(IOStatePtr self, ConstBStringPtr source_path, ConstBStringPtr target_path)
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
                        &source_route.subpath,
                        &target_route.subpath
                    );
                },
                none() { return Error(IOStatus, BString__sprintf(&invalid_path_format, target_path), IO_INVALID_PATH); }
            );
        },
        none() { return Error(IOStatus, BString__sprintf(&invalid_path_format, source_path), IO_INVALID_PATH); }
    );

    return Error(IOStatus, BString__sprintf(&invalid_path_format, source_path), IO_INVALID_PATH);
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
        .base_io = IO__new(
            RootFileIO__open,
            RootFileIO__read,
            RootFileIO__write,
            IO__unsupported_accept,
            RootFileIO__close,
            RootFileIO__park,
            RootFileIO__unpark,
            IO__unsupported_server_buffer,
            IO__unsupported_server_write_buffer,
            IO__unsupported_data_available,
            IO__unsupported_data_consumed,
            IO__unsupported_eof
        ),
        .list_nodes = RootFileIO__list_nodes,
        .list_dir = RootFileIO__list_dir,
        .mkdir = RootFileIO__mkdir,
        .remove = RootFileIO__remove,
        .link = RootFileIO__link,
    };

    return root_file_io;
}

static FilesystemCode fs_codes = 0x01;


FilesystemPtr Filesystem__heap_new(ConstBStringPtr name, const FileIO * file_io)
{
    FilesystemPtr fs = ptr(Kernel__alloc_kernel(sizeof(Filesystem)), Filesystem);
    FilesystemCode code = fs_codes++;

    *fs = (Filesystem) {
        .name = *name,
        .mounts = Mounts__new(),
        .filesystem_table = FilesystemTable__new(),
        .file_io = *file_io,
        .fs_code = (code << CODE_SHIFT),
    };
    __d_printf(FILESYSTEM, "Created filesystem %x %p with code %x", sizeof(Filesystem), fs, code);
    return fs;
}

FilesystemPtr RootFilesystem__heap_new()
{
    FileIO file_io = RootFilesystemIO__new();
    return Filesystem__heap_new(&bstr("root"), &file_io);
}

Result(MountResult) Filesystem__mount(FilesystemPtr self, ConstBStringPtr path, FilesystemPtr filesystem)
{
    Mounts__set(&self->mounts, *path, filesystem);

    return Success(MountResult, MOUNT_OK);
}

FileHandleTargetPtr FileHandleTarget__heap_new(ConstBStringPtr path)
{
    FileHandleTargetPtr target = ptr(Kernel__alloc_kernel(sizeof(FileHandleTarget)), FileHandleTarget);
    *target = (FileHandleTarget) {
        .path = *path,
        .offset = 0,
        .buffer = EmptyRingBuffer(IOBuffer),
    };

    return target;
}

void FileHandleTarget__dispose(FileHandleTargetPtr self)
{
    Kernel__free(addr(self));
}
