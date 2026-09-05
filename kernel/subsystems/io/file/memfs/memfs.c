#include "memfs.h"

#include "../../../../kalloc.h"
#include "../../../../utils/math.h"

static BString invalid_path_format = bstr("Invalid path %B");
static BString invalid_file_handle = bstr("Invalid file handle");
static BString not_implemented = bstr("Operation not implemented");
static BString end_of_file = bstr("End of file");
static BString allocation_failed = bstr("Allocation failed");
static BString corrupt_file = bstr("Corrupt file");
static BString directory_not_empty = bstr("Directory not empty");

bool MemFilePage__equals(MemFilePagePtr self, MemFilePagePtr other)
{
    return self->address.virtual_addr == other->address.virtual_addr;
}

LinkedListBodyDefine(MemFilePages);

bool MemFile__equals(MemFilePtr self, MemFilePtr other)
{
    return BString__equals(&self->name, &other->name);
}

LinkedListBodyDefine(MemFiles);
LinkedListBodyDefine(MemDirectories);
MapBodyDefine(MemFileHandleMap);

static MemFilePage MemFilePage__new(KernelAddress address)
{
    return (MemFilePage) {
        .address = address,
    };
}

static MemFile MemFile__new(ConstBStringPtr name)
{
    return (MemFile) {
        .name = *name,
        .size = 0,
        .capacity = 0,
        .pages = MemFilePages__new(),
    };
}

static MemDirectory MemDirectory__new(ConstBStringPtr path)
{
    return (MemDirectory) {
        .path = *path,
    };
}

static bool MemFile__matches_name(MemFilePtr file, void * context)
{
    BStringPtr name = (BStringPtr) context;
    return BString__equals(&file->name, name);
}

static LLFindResultOptional(MemFiles) MemFilesystem__find_file(MemFilesystemIOStatePtr self, BStringPtr path)
{
    return MemFiles__find(&self->files, MemFile__matches_name, path);
}

bool MemDirectory__equals(MemDirectoryPtr self, MemDirectoryPtr other)
{
    return BString__equals(&self->path, &other->path);
}

static bool MemDirectory__matches_path(MemDirectoryPtr directory, void * context)
{
    BStringPtr path = (BStringPtr) context;
    return BString__equals(&directory->path, path);
}

static LLFindResultOptional(MemDirectories) MemFilesystem__find_directory(
    MemFilesystemIOStatePtr self, BStringPtr path
)
{
    return MemDirectories__find(&self->directories, MemDirectory__matches_path, path);
}

static BString MemFilesystem__parent_path(ConstBStringPtr path)
{
    int64_t last_separator = -1;

    for (StringPos i = 0; i < BString__length(path); i++)
    {
        if (BString__char_at_or(path, i, '\0') == '/')
        {
            last_separator = (int64_t) i;
        }
    }

    if (last_separator <= 0)
    {
        return empty_bstr();
    }

    return BString__slice(path, 0, last_separator);
}

static BString MemFilesystem__basename(ConstBStringPtr path)
{
    StringPos start = 0;

    for (StringPos i = 0; i < BString__length(path); i++)
    {
        if (BString__char_at_or(path, i, '\0') == '/')
        {
            start = i + 1;
        }
    }

    return BString__slice(path, start, -1);
}

static bool MemFilesystem__directory_exists(MemFilesystemIOStatePtr self, BStringPtr path)
{
    if (BString__is_empty(path))
    {
        return true;
    }

    match_find_result(MemDirectories, MemFilesystem__find_directory(self, path),
        some(_) { return true; },
        none_do_nothing()
    );

    return false;
}

static bool MemFilesystem__has_child(MemFilesystemIOStatePtr self, BStringPtr path)
{
    for_each_ll(MemDirectories, self->directories, directory,
    {
        BString parent = MemFilesystem__parent_path(&directory.path);
        if (BString__equals(&parent, path))
        {
            return true;
        }
    });

    for_each_ll(MemFiles, self->files, file,
    {
        BString parent = MemFilesystem__parent_path(&file.name);
        if (BString__equals(&parent, path))
        {
            return true;
        }
    });

    return false;
}

static bool MemFilesystem__path_is_child_of(BStringPtr child, BStringPtr parent)
{
    if (BString__length(child) <= BString__length(parent))
    {
        return false;
    }

    if (!BString__is_empty(parent))
    {
        if (!BString__starts_with(child, parent))
        {
            return false;
        }

        return BString__char_at_or(child, BString__length(parent), '\0') == '/';
    }

    return true;
}

static Result(IOStatus) MemFilesystem__remove_file(MemFilesystemIOStatePtr self, BStringPtr path)
{
    match_find_result(MemFiles, MemFilesystem__find_file(self, path),
        some(file)
        {
            match_result(CollectionCode, MemFiles__remove(&self->files, file),
                success(_) { return Success(IOStatus, IO_OK); },
                error(code, message) { return Error(IOStatus, message, code); }
            );
        },
        none_do_nothing()
    );

    return Error(IOStatus, BString__sprintf(&invalid_path_format, path), IO_INVALID_PATH);
}

static Result(IOStatus) MemFilesystem__remove_directory_entry(MemFilesystemIOStatePtr self, BStringPtr path)
{
    match_find_result(MemDirectories, MemFilesystem__find_directory(self, path),
        some(directory)
        {
            match_result(CollectionCode, MemDirectories__remove(&self->directories, directory),
                success(_) { return Success(IOStatus, IO_OK); },
                error(code, message) { return Error(IOStatus, message, code); }
            );
        },
        none_do_nothing()
    );

    return Error(IOStatus, BString__sprintf(&invalid_path_format, path), IO_INVALID_PATH);
}

static Result(IOStatus) MemFilesystem__remove_recursive(MemFilesystemIOStatePtr self, BStringPtr path)
{
    bool removed_child = true;
    while (removed_child)
    {
        removed_child = false;

        for_each_ll(MemFiles, self->files, file,
        {
            if (MemFilesystem__path_is_child_of(&file.name, path))
            {
                match_result(IOStatus, MemFilesystem__remove_file(self, &file.name),
                    success(_) { removed_child = true;; },
                    error(code, message) { return Error(IOStatus, message, code); }
                );
                break;
            }
        });

        if (removed_child)
        {
            continue;
        }

        for_each_ll(MemDirectories, self->directories, directory,
        {
            if (MemFilesystem__path_is_child_of(&directory.path, path))
            {
                match_result(IOStatus, MemFilesystem__remove_directory_entry(self, &directory.path),
                    success(_) { removed_child = true;; },
                    error(code, message) { return Error(IOStatus, message, code); }
                );
                break;
            }
        });
    }

    return MemFilesystem__remove_directory_entry(self, path);
}

static Result(CollectionCode) MemFile__ensure_capacity(MemFilePtr self, size_t required_capacity)
{
    while (self->capacity < required_capacity)
    {
        KernelAddress page = Kernel__alloc_page();
        if (!page.valid)
        {
            return Error(CollectionCode, allocation_failed, IO_OPERATION_NOT_SUPPORTED);
        }

        DataPtr page_data = v_ptr(page, Data);
        for (size_t i = 0; i < PAGE_SIZE; i++)
        {
            page_data[i] = 0;
        }

        match_result(CollectionCode, MemFilePages__push(&self->pages, MemFilePage__new(page)),
            success(_) { self->capacity += PAGE_SIZE;; },
            error(code, message) { return Error(CollectionCode, message, code); }
        );
    }

    return Success(CollectionCode, 0);
}

static Optional(MemFilePage) MemFile__page(MemFilePtr self, size_t page_index)
{
    return MemFilePages__at(&self->pages, page_index);
}

static Result(CollectionCode) MemFile__copy_from_file(MemFilePtr self, size_t offset, DataPtr target, size_t size)
{
    for (size_t i = 0; i < size; i++)
    {
        size_t file_offset = offset + i;
        match_optional(MemFilePage, MemFile__page(self, file_offset / PAGE_SIZE),
            some(file_page)
            {
                DataPtr page = v_ptr(file_page.address, Data);
                target[i] = page[file_offset % PAGE_SIZE];
            },
            none() { return Error(CollectionCode, corrupt_file, IO_INVALID_PATH); }
        );
    }

    return Success(CollectionCode, 0);
}

static Result(CollectionCode) MemFile__copy_to_file(MemFilePtr self, size_t offset, DataPtr source, size_t size)
{
    for (size_t i = 0; i < size; i++)
    {
        size_t file_offset = offset + i;
        match_optional(MemFilePage, MemFile__page(self, file_offset / PAGE_SIZE),
            some(file_page)
            {
                DataPtr page = v_ptr(file_page.address, Data);
                page[file_offset % PAGE_SIZE] = source[i];
            },
            none() { return Error(CollectionCode, corrupt_file, IO_INVALID_PATH); }
        );
    }

    return Success(CollectionCode, 0);
}

static Result(MemFilePtr) MemFilesystem__create_file(MemFilesystemIOStatePtr self, ConstBStringPtr path)
{
    MemFile file = MemFile__new(path);

    match_result(CollectionCode, MemFiles__push(&self->files, file),
        success(_)
        {
            match_find_result_ptr(MemFiles, MemFilesystem__find_file(self, (BStringPtr) path),
                some(file) { return Success(MemFilePtr, file); },
                none_do_nothing()
            );
        },
        error(code, message) { return Error(MemFilePtr, message, code); }
    );

    return Error(MemFilePtr, allocation_failed, IO_OPERATION_NOT_SUPPORTED);
}

static Result(MemFilePtr) MemFilesystem__get_or_create_file(
    MemFilesystemIOStatePtr self, ConstBStringPtr path, Flags flags
)
{
    match_find_result_ptr(MemFiles, MemFilesystem__find_file(self, (BStringPtr) path),
        some(file)
        {
            if ((flags & IO_OPEN_WRITE) != 0)
            {
                file->size = 0;
            }

            return Success(MemFilePtr, file);
        },
        none_do_nothing()
    );

    if ((flags & IO_OPEN_WRITE) == 0)
    {
        return Error(MemFilePtr, BString__sprintf(&invalid_path_format, path), IO_INVALID_PATH);
    }

    return MemFilesystem__create_file(self, path);
}

static MemFileHandlePtr MemFileHandle__heap_new(ConstBStringPtr path, Flags flags)
{
    MemFileHandlePtr target = ptr(Kernel__alloc_kernel(sizeof(MemFileHandle)), MemFileHandle);
    *target = (MemFileHandle) {
        .path = *path,
        .offset = 0,
        .buffer = EmptyRingBuffer(IOBuffer),
        .flags = flags,
    };

    return target;
}

static Result(BString) MemFilesystem__file_path(ConstBStringPtr path)
{
    BString name = MemFilesystem__basename(path);
    if (BString__is_empty(&name))
    {
        return Error(BString, BString__sprintf(&invalid_path_format, path), IO_INVALID_PATH);
    }

    return Success(BString, *path);
}

Result(IOInternalHandle) MemFileIO__open(IOStatePtr self, ConstBStringPtr path, Flags flags)
{
    MemFilesystemIOStatePtr fs_state = (MemFilesystemIOStatePtr) self;

    match_result(BString, MemFilesystem__file_path(path),
        success(file_path)
        {
            critical_section(fs_state->state.lock,
            {
                BString parent = MemFilesystem__parent_path(&file_path);
                if (!MemFilesystem__directory_exists(fs_state, &parent))
                {
                    cs_return Error(IOInternalHandle, BString__sprintf(&invalid_path_format, path), IO_INVALID_PATH);
                }

                match_find_result(MemDirectories, MemFilesystem__find_directory(fs_state, &file_path),
                    some(_) { cs_return Error(IOInternalHandle, BString__sprintf(&invalid_path_format, path), IO_INVALID_PATH); },
                    none_do_nothing()
                );

                match_result(MemFilePtr, MemFilesystem__get_or_create_file(fs_state, &file_path, flags),
                    success(_)
                    {
                        IOInternalHandle handle =
                            (IOInternalHandle) fs_state->handle_count++ | fs_state->state.filesystem->fs_code;
                        MemFileHandleMap__set(&fs_state->handles, handle, MemFileHandle__heap_new(&file_path, flags));
                        cs_return Success(IOInternalHandle, handle);
                    },
                    error(code, message) { cs_return Error(IOInternalHandle, message, code); }
                );
            });
        },
        error(code, message) { return Error(IOInternalHandle, message, code); }
    );

    return Error(IOInternalHandle, BString__sprintf(&invalid_path_format, path), IO_INVALID_PATH);
}

Result(IOInternalHandle) MemFileIO__close(IOStatePtr self, IOInternalHandle handle)
{
    MemFilesystemIOStatePtr fs_state = (MemFilesystemIOStatePtr) self;

    critical_section(fs_state->state.lock,
    {
        match_optional(MemFileHandlePtr, MemFileHandleMap__get(&fs_state->handles, handle),
            some(_)
            {
                MemFileHandleMap__delete(&fs_state->handles, handle);
                cs_return Success(IOInternalHandle, handle);
            },
            none() { cs_return Error(IOInternalHandle, invalid_file_handle, IO_UNKNOWN_HANDLE); }
        );
    });

    return Error(IOInternalHandle, invalid_file_handle, IO_UNKNOWN_HANDLE);
}

Result(IOBufferPtr) MemFileIO__read(
    IOStatePtr self, IOInternalHandle handle, size_t size, ConstExternalBufferPtr target_buffer
)
{
    MemFilesystemIOStatePtr fs_state = (MemFilesystemIOStatePtr) self;

    critical_section(fs_state->state.lock,
    {
        match_optional(MemFileHandlePtr, MemFileHandleMap__get(&fs_state->handles, handle),
            some(target)
            {
                if ((target->flags & IO_OPEN_READ) == 0)
                {
                    cs_return Error(IOBufferPtr, invalid_file_handle, IO_OPERATION_NOT_SUPPORTED);
                }

                match_find_result_ptr(MemFiles, MemFilesystem__find_file(fs_state, &target->path),
                    some(file)
                    {
                        if (target->offset >= file->size)
                        {
                            cs_return Error(IOBufferPtr, end_of_file, IO_EOF);
                        }

                        size_t remaining = file->size - target->offset;
                        size_t free_space = IOBuffer__free_space(&target->buffer);
                        size_t read_size = min(size, min(remaining, free_space));

                        Data buffer[PAGE_SIZE];
                        size_t copied = 0;
                        while (copied < read_size)
                        {
                            size_t chunk_size = min(PAGE_SIZE, read_size - copied);
                            match_result(CollectionCode, MemFile__copy_from_file(file, target->offset + copied, buffer, chunk_size),
                                success_do_nothing(),
                                error(code, message) { cs_return Error(IOBufferPtr, message, code); }
                            );
                            IOBuffer__push_all(&target->buffer, buffer, chunk_size);
                            copied += chunk_size;
                        }
                        target->offset += read_size;

                        cs_return Success(IOBufferPtr, &target->buffer);
                    },
                    none() { cs_return Error(IOBufferPtr, BString__sprintf(&invalid_path_format, &target->path), IO_INVALID_PATH); }
                );
            },
            none() { cs_return Error(IOBufferPtr, invalid_file_handle, IO_UNKNOWN_HANDLE); }
        );
    });

    return Error(IOBufferPtr, invalid_file_handle, IO_UNKNOWN_HANDLE);
}

Result(ByteCount) MemFileIO__write(IOStatePtr self, IOInternalHandle handle, DataPtr data, size_t size)
{
    MemFilesystemIOStatePtr fs_state = (MemFilesystemIOStatePtr) self;

    critical_section(fs_state->state.lock,
    {
        match_optional(MemFileHandlePtr, MemFileHandleMap__get(&fs_state->handles, handle),
            some(target)
            {
                if ((target->flags & IO_OPEN_WRITE) == 0)
                {
                    cs_return Error(ByteCount, invalid_file_handle, IO_OPERATION_NOT_SUPPORTED);
                }

                match_find_result_ptr(MemFiles, MemFilesystem__find_file(fs_state, &target->path),
                    some(file)
                    {
                        match_result(CollectionCode, MemFile__ensure_capacity(file, target->offset + size),
                            success_do_nothing(),
                            error(code, message) { cs_return Error(ByteCount, message, code); }
                        );

                        match_result(CollectionCode, MemFile__copy_to_file(file, target->offset, data, size),
                            success_do_nothing(),
                            error(code, message) { cs_return Error(ByteCount, message, code); }
                        );

                        target->offset += size;
                        if (target->offset > file->size)
                        {
                            file->size = target->offset;
                        }

                        cs_return Success(ByteCount, size);
                    },
                    none() { cs_return Error(ByteCount, BString__sprintf(&invalid_path_format, &target->path), IO_INVALID_PATH); }
                );
            },
            none() { cs_return Error(ByteCount, invalid_file_handle, IO_UNKNOWN_HANDLE); }
        );
    });

    return Error(ByteCount, invalid_file_handle, IO_UNKNOWN_HANDLE);
}

Result(ParkStatus) MemFileIO__park(IOStatePtr self, IOContextPtr parked_context)
{
    return Error(ParkStatus, not_implemented, IO_OPERATION_NOT_IMPLEMENTED);
}

Result(ParkStatus) MemFileIO__unpark(IOStatePtr self)
{
    return Error(ParkStatus, not_implemented, IO_OPERATION_NOT_IMPLEMENTED);
}

Result(FilesystemNodes) MemFileIO__list_nodes(IOStatePtr self, FilesystemNodePtr parent, ConstBStringPtr path)
{
    MemFilesystemIOStatePtr fs_state = (MemFilesystemIOStatePtr) self;
    FilesystemNodes result = EmptyArray(FilesystemNodes);

    critical_section(fs_state->state.lock,
    {
        if (!MemFilesystem__directory_exists(fs_state, (BStringPtr) path))
        {
            cs_return Error(FilesystemNodes, BString__sprintf(&invalid_path_format, path), IO_INVALID_PATH);
        }

        for_each_ll(MemDirectories, fs_state->directories, directory,
        {
            BString directory_parent = MemFilesystem__parent_path(&directory.path);
            if (BString__equals(&directory_parent, path))
            {
                BString directory_name = MemFilesystem__basename(&directory.path);
                FilesystemNodePtr node = FilesystemNode__heap_new(
                    fs_state->state.filesystem, &directory_name, 0
                );
                FilesystemNodes__push(&result, node);
            }
        });

        for_each_ll(MemFiles, fs_state->files, file,
        {
            BString file_parent = MemFilesystem__parent_path(&file.name);
            if (BString__equals(&file_parent, path))
            {
                BString file_name = MemFilesystem__basename(&file.name);
                FilesystemNodePtr node = FilesystemNode__heap_new(
                    fs_state->state.filesystem, &file_name, 0
                );
                FilesystemNodes__push(&result, node);
            }
        });
    });

    return Success(FilesystemNodes, result);
}

Result(IOStatus) MemFileIO__mkdir(IOStatePtr self, ConstBStringPtr path)
{
    MemFilesystemIOStatePtr fs_state = (MemFilesystemIOStatePtr) self;
    BString directory_path = *path;
    BString basename = MemFilesystem__basename(&directory_path);
    if (BString__is_empty(&basename))
    {
        return Error(IOStatus, BString__sprintf(&invalid_path_format, path), IO_INVALID_PATH);
    }

    critical_section(fs_state->state.lock,
    {
        BString parent = MemFilesystem__parent_path(&directory_path);
        if (!MemFilesystem__directory_exists(fs_state, &parent))
        {
            cs_return Error(IOStatus, BString__sprintf(&invalid_path_format, path), IO_INVALID_PATH);
        }

        match_find_result(MemDirectories, MemFilesystem__find_directory(fs_state, &directory_path),
            some(_) { cs_return Error(IOStatus, BString__sprintf(&invalid_path_format, path), IO_INVALID_PATH); },
            none_do_nothing()
        );

        match_find_result(MemFiles, MemFilesystem__find_file(fs_state, &directory_path),
            some(_) { cs_return Error(IOStatus, BString__sprintf(&invalid_path_format, path), IO_INVALID_PATH); },
            none_do_nothing()
        );

        match_result(CollectionCode, MemDirectories__push(&fs_state->directories, MemDirectory__new(&directory_path)),
            success(_) { cs_return Success(IOStatus, IO_OK); },
            error(code, message) { cs_return Error(IOStatus, message, code); }
        );
    });

    return Error(IOStatus, BString__sprintf(&invalid_path_format, path), IO_INVALID_PATH);
}

Result(IOStatus) MemFileIO__remove(IOStatePtr self, ConstBStringPtr path, Flags flags)
{
    MemFilesystemIOStatePtr fs_state = (MemFilesystemIOStatePtr) self;
    BString file_path = *path;

    critical_section(fs_state->state.lock,
    {
        match_result(IOStatus, MemFilesystem__remove_file(fs_state, &file_path),
            success(status) { cs_return Success(IOStatus, status); },
            error(code, message)
            {
                if (code != IO_INVALID_PATH)
                {
                    cs_return Error(IOStatus, message, code);
                }
            }
        );

        match_find_result(MemDirectories, MemFilesystem__find_directory(fs_state, &file_path),
            some(_)
            {
                if ((flags & IO_REMOVE_RECURSIVE) != 0)
                {
                    match_result(IOStatus, MemFilesystem__remove_recursive(fs_state, &file_path),
                        success(status) { cs_return Success(IOStatus, status); },
                        error(code, message) { cs_return Error(IOStatus, message, code); }
                    );
                }

                if (MemFilesystem__has_child(fs_state, &file_path))
                {
                    cs_return Error(IOStatus, directory_not_empty, IO_OPERATION_NOT_SUPPORTED);
                }
                cs_return Error(IOStatus, bstring("remove directory not supported"), IO_OPERATION_NOT_SUPPORTED);
            },
            none_do_nothing()
        );
    });

    return Error(IOStatus, BString__sprintf(&invalid_path_format, path), IO_INVALID_PATH);
}

FileIO MemFilesystemIO__new()
{
    FileIO mem_file_io = (FileIO) {
        .base_io = IO__new(
            MemFileIO__open,
            MemFileIO__read,
            MemFileIO__write,
            IO__unsupported_accept,
            MemFileIO__close,
            MemFileIO__park,
            MemFileIO__unpark,
            IO__unsupported_server_buffer,
            IO__unsupported_server_write_buffer,
            IO__unsupported_data_available,
            IO__unsupported_data_consumed,
            IO__unsupported_eof
        ),
        .list_nodes = MemFileIO__list_nodes,
        .list_dir = FileIO__unsupported_list_dir,
        .mkdir = MemFileIO__mkdir,
        .remove = MemFileIO__remove,
        .link = FileIO__unsupported_link,
    };
    return mem_file_io;
}

MemFilesystemIOStatePtr MemFilesystemIOState__heap_new(FilesystemPtr fs)
{
    MemFilesystemIOStatePtr io_state = ptr(
            Kernel__alloc_kernel(sizeof(MemFilesystemIOState)), MemFilesystemIOState
        );

    *io_state = (MemFilesystemIOState) {
        .state = FilesystemIOState__new(fs),
        .files = MemFiles__new(),
        .directories = MemDirectories__new(),
        .handles = MemFileHandleMap__new(),
        .handle_count = 0,
    };

    fs->state = &io_state->state;

    return io_state;
}
