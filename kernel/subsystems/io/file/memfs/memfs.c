#include "memfs.h"

#include "../../../../kalloc.h"
#include "../../../../utils/math.h"

static KString invalid_path_format = KSTRING_NEW("Invalid path %s");
static KString invalid_file_handle = KSTRING_NEW("Invalid file handle");
static KString not_implemented = KSTRING_NEW("Operation not implemented");
static KString end_of_file = KSTRING_NEW("End of file");
static KString allocation_failed = KSTRING_NEW("Allocation failed");
static KString corrupt_file = KSTRING_NEW("Corrupt file");
static KString directory_not_empty = KSTRING_NEW("Directory not empty");

bool MemFilePage__equals(MemFilePagePtr self, MemFilePagePtr other)
{
    return self->address.virtual_addr == other->address.virtual_addr;
}

LinkedListBodyDefine(MemFilePages);

bool MemFile__equals(MemFilePtr self, MemFilePtr other)
{
    return KString__equals(&self->name, &other->name);
}

LinkedListBodyDefine(MemFiles);
LinkedListBodyDefine(MemDirectories);
MapBodyDefine(MemFileHandleMap);

static bool MemFile__matches_name(MemFilePtr file, void * context)
{
    KStringPtr name = (KStringPtr) context;
    return KString__equals(&file->name, name);
}

static LLFindResultOptional(MemFiles) MemFilesystem__find_file(MemFilesystemIOStatePtr self, KStringPtr path)
{
    return MemFiles__find(&self->files, MemFile__matches_name, path);
}

bool MemDirectory__equals(MemDirectoryPtr self, MemDirectoryPtr other)
{
    return KString__equals(&self->path, &other->path);
}

static bool MemDirectory__matches_path(MemDirectoryPtr directory, void * context)
{
    KStringPtr path = (KStringPtr) context;
    return KString__equals(&directory->path, path);
}

static LLFindResultOptional(MemDirectories) MemFilesystem__find_directory(
    MemFilesystemIOStatePtr self, KStringPtr path
)
{
    return MemDirectories__find(&self->directories, MemDirectory__matches_path, path);
}

static KString MemFilesystem__parent_path(KString path)
{
    KString result = EMPTY_KSTRING;
    int64_t last_separator = -1;

    for (StringPos i = 0; i < path.len; i++)
    {
        if (path.data[i] == '/')
        {
            last_separator = (int64_t) i;
        }
    }

    if (last_separator <= 0)
    {
        return result;
    }

    for (int64_t i = 0; i < last_separator && i < (int64_t) result.max - 1; i++)
    {
        result.data[i] = path.data[i];
        result.len++;
    }
    result.data[result.len] = '\0';

    return result;
}

static KString MemFilesystem__basename(KString path)
{
    KString result = EMPTY_KSTRING;
    StringPos start = 0;

    for (StringPos i = 0; i < path.len; i++)
    {
        if (path.data[i] == '/')
        {
            start = i + 1;
        }
    }

    for (StringPos i = start; i < path.len && result.len < result.max - 1; i++)
    {
        result.data[result.len++] = path.data[i];
    }
    result.data[result.len] = '\0';

    return result;
}

static bool MemFilesystem__directory_exists(MemFilesystemIOStatePtr self, KStringPtr path)
{
    if (path->len == 0)
    {
        return true;
    }

    LLFindResultMatch(MemDirectories, MemFilesystem__find_directory(self, path))
        IsSome(_, return true)
    End

    return false;
}

static bool MemFilesystem__has_child(MemFilesystemIOStatePtr self, KStringPtr path)
{
    LLIterate(MemDirectories, self->directories, directory)
        KString parent = MemFilesystem__parent_path(directory.path);
        if (KString__equals(&parent, path))
        {
            return true;
        }
    End

    LLIterate(MemFiles, self->files, file)
        KString parent = MemFilesystem__parent_path(file.name);
        if (KString__equals(&parent, path))
        {
            return true;
        }
    End

    return false;
}

static bool MemFilesystem__path_is_child_of(KStringPtr child, KStringPtr parent)
{
    if (child->len <= parent->len)
    {
        return false;
    }

    if (parent->len > 0)
    {
        for (StringPos i = 0; i < parent->len; i++)
        {
            if (child->data[i] != parent->data[i])
            {
                return false;
            }
        }

        return child->data[parent->len] == '/';
    }

    return true;
}

static Result(IOStatus) MemFilesystem__remove_file(MemFilesystemIOStatePtr self, KStringPtr path)
{
    LLFindResultMatch(MemFiles, MemFilesystem__find_file(self, path))
        IsSome(file_match,
        {
            ResultMatch(CollectionCode, MemFiles__remove(&self->files, *file_match.value))
                IsSuccess(_, return Success(IOStatus, IO_OK))
                IsError(code, message, return Error(IOStatus, message, code))
            End
        })
    End

    return Error(IOStatus, KString__sprintf(&invalid_path_format, path), IO_INVALID_PATH);
}

static Result(IOStatus) MemFilesystem__remove_directory_entry(MemFilesystemIOStatePtr self, KStringPtr path)
{
    LLFindResultMatch(MemDirectories, MemFilesystem__find_directory(self, path))
        IsSome(directory_match,
        {
            ResultMatch(CollectionCode, MemDirectories__remove(&self->directories, *directory_match.value))
                IsSuccess(_, return Success(IOStatus, IO_OK))
                IsError(code, message, return Error(IOStatus, message, code))
            End
        })
    End

    return Error(IOStatus, KString__sprintf(&invalid_path_format, path), IO_INVALID_PATH);
}

static Result(IOStatus) MemFilesystem__remove_recursive(MemFilesystemIOStatePtr self, KStringPtr path)
{
    bool removed_child = true;
    while (removed_child)
    {
        removed_child = false;

        LLIterate(MemFiles, self->files, file)
            if (MemFilesystem__path_is_child_of(&file.name, path))
            {
                ResultMatch(IOStatus, MemFilesystem__remove_file(self, &file.name))
                    IsSuccess(_, removed_child = true;)
                    IsError(code, message, return Error(IOStatus, message, code))
                End
                break;
            }
        End

        if (removed_child)
        {
            continue;
        }

        LLIterate(MemDirectories, self->directories, directory)
            if (MemFilesystem__path_is_child_of(&directory.path, path))
            {
                ResultMatch(IOStatus, MemFilesystem__remove_directory_entry(self, &directory.path))
                    IsSuccess(_, removed_child = true;)
                    IsError(code, message, return Error(IOStatus, message, code))
                End
                break;
            }
        End
    }

    return MemFilesystem__remove_directory_entry(self, path);
}

static Result(CollectionCode) MemFile__ensure_capacity(MemFilePtr self, size_t required_capacity)
{
    while (self->capacity < required_capacity)
    {
        KernelAddress page = Kernel__alloc(PAGE_SIZE);
        if (!page.valid)
        {
            return Error(CollectionCode, allocation_failed, IO_OPERATION_NOT_SUPPORTED);
        }

        DataPtr page_data = v_ptr(page, Data);
        for (size_t i = 0; i < PAGE_SIZE; i++)
        {
            page_data[i] = 0;
        }

        ResultMatch(CollectionCode, MemFilePages__push(&self->pages, ((MemFilePage) { .address = page })))
            IsSuccess(_, self->capacity += PAGE_SIZE;)
            IsError(code, message, return Error(CollectionCode, message, code))
        End
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
        Match(MemFilePage, MemFile__page(self, file_offset / PAGE_SIZE))
            IsNone(return Error(CollectionCode, corrupt_file, IO_INVALID_PATH))
            IsSome(file_page,
            {
                DataPtr page = v_ptr(file_page.address, Data);
                target[i] = page[file_offset % PAGE_SIZE];
            })
        End
    }

    return Success(CollectionCode, 0);
}

static Result(CollectionCode) MemFile__copy_to_file(MemFilePtr self, size_t offset, DataPtr source, size_t size)
{
    for (size_t i = 0; i < size; i++)
    {
        size_t file_offset = offset + i;
        Match(MemFilePage, MemFile__page(self, file_offset / PAGE_SIZE))
            IsNone(return Error(CollectionCode, corrupt_file, IO_INVALID_PATH))
            IsSome(file_page,
            {
                DataPtr page = v_ptr(file_page.address, Data);
                page[file_offset % PAGE_SIZE] = source[i];
            })
        End
    }

    return Success(CollectionCode, 0);
}

static Result(MemFilePtr) MemFilesystem__create_file(MemFilesystemIOStatePtr self, KString path)
{
    MemFile file = (MemFile) {
        .name = path,
        .size = 0,
        .capacity = 0,
        .pages = MemFilePages__new(),
    };

    ResultMatch(CollectionCode, MemFiles__push(&self->files, file))
        IsSuccess(_,
        {
            LLFindResultMatch(MemFiles, MemFilesystem__find_file(self, &path))
                IsSome(match, return Success(MemFilePtr, match.value))
            End
        })
        IsError(code, message, return Error(MemFilePtr, message, code))
    End

    return Error(MemFilePtr, allocation_failed, IO_OPERATION_NOT_SUPPORTED);
}

static Result(MemFilePtr) MemFilesystem__get_or_create_file(
    MemFilesystemIOStatePtr self, KString path, Flags flags
)
{
    LLFindResultMatch(MemFiles, MemFilesystem__find_file(self, &path))
        IsSome(match,
        {
            if ((flags & IO_OPEN_WRITE) != 0)
            {
                match.value->size = 0;
            }

            return Success(MemFilePtr, match.value);
        })
    End

    if ((flags & IO_OPEN_WRITE) == 0)
    {
        return Error(MemFilePtr, KString__sprintf(&invalid_path_format, &path), IO_INVALID_PATH);
    }

    return MemFilesystem__create_file(self, path);
}

static MemFileHandlePtr MemFileHandle__heap_new(KString path, Flags flags)
{
    MemFileHandlePtr target = ptr(Kernel__alloc_kernel(sizeof(MemFileHandle)), MemFileHandle);
    *target = (MemFileHandle) {
        .path = path,
        .offset = 0,
        .buffer = EmptyRingBuffer(IOBuffer),
        .flags = flags,
    };

    return target;
}

static Result(KString) MemFilesystem__file_path(KString path)
{
    KString name = MemFilesystem__basename(path);
    if (name.len == 0)
    {
        return Error(KString, KString__sprintf(&invalid_path_format, &path), IO_INVALID_PATH);
    }

    return Success(KString, path);
}

Result(IOInternalHandle) MemFileIO__open(IOStatePtr self, KString path, Flags flags)
{
    MemFilesystemIOStatePtr fs_state = (MemFilesystemIOStatePtr) self;

    ResultMatch(KString, MemFilesystem__file_path(path))
        IsSuccess(file_path,
        {
            CriticalSection(fs_state->state.lock,
            {
                KString parent = MemFilesystem__parent_path(file_path);
                if (!MemFilesystem__directory_exists(fs_state, &parent))
                {
                    _cs_return Error(IOInternalHandle, KString__sprintf(&invalid_path_format, &path), IO_INVALID_PATH);
                }

                LLFindResultMatch(MemDirectories, MemFilesystem__find_directory(fs_state, &file_path))
                    IsSome(_, _cs_return Error(IOInternalHandle, KString__sprintf(&invalid_path_format, &path), IO_INVALID_PATH))
                End

                ResultMatch(MemFilePtr, MemFilesystem__get_or_create_file(fs_state, file_path, flags))
                    IsSuccess(_,
                    {
                        IOInternalHandle handle =
                            (IOInternalHandle) fs_state->handle_count++ | fs_state->state.filesystem->fs_code;
                        MemFileHandleMap__set(&fs_state->handles, handle, MemFileHandle__heap_new(file_path, flags));
                        _cs_return Success(IOInternalHandle, handle);
                    })
                    IsError(code, message, _cs_return Error(IOInternalHandle, message, code))
                End
            })
        })
        IsError(code, message, return Error(IOInternalHandle, message, code))
    End

    return Error(IOInternalHandle, KString__sprintf(&invalid_path_format, &path), IO_INVALID_PATH);
}

Result(IOInternalHandle) MemFileIO__close(IOStatePtr self, IOInternalHandle handle)
{
    MemFilesystemIOStatePtr fs_state = (MemFilesystemIOStatePtr) self;

    CriticalSection(fs_state->state.lock,
    {
        Match(MemFileHandlePtr, MemFileHandleMap__get(&fs_state->handles, handle))
            IsNone(_cs_return Error(IOInternalHandle, invalid_file_handle, IO_UNKNOWN_HANDLE))
            IsSome(_,
            {
                MemFileHandleMap__delete(&fs_state->handles, handle);
                _cs_return Success(IOInternalHandle, handle);
            })
        End
    })

    return Error(IOInternalHandle, invalid_file_handle, IO_UNKNOWN_HANDLE);
}

Result(IOBufferPtr) MemFileIO__read(
    IOStatePtr self, IOInternalHandle handle, size_t size, ExternalBuffer target_buffer
)
{
    MemFilesystemIOStatePtr fs_state = (MemFilesystemIOStatePtr) self;

    CriticalSection(fs_state->state.lock,
    {
        Match(MemFileHandlePtr, MemFileHandleMap__get(&fs_state->handles, handle))
            IsNone(_cs_return Error(IOBufferPtr, invalid_file_handle, IO_UNKNOWN_HANDLE))
            IsSome(target,
            {
                if ((target->flags & IO_OPEN_READ) == 0)
                {
                    _cs_return Error(IOBufferPtr, invalid_file_handle, IO_OPERATION_NOT_SUPPORTED);
                }

                LLFindResultMatch(MemFiles, MemFilesystem__find_file(fs_state, &target->path))
                    IsNone(_cs_return Error(IOBufferPtr, KString__sprintf(&invalid_path_format, &target->path), IO_INVALID_PATH))
                    IsSome(file_match,
                    {
                        MemFilePtr file = file_match.value;
                        if (target->offset >= file->size)
                        {
                            _cs_return Error(IOBufferPtr, end_of_file, IO_EOF);
                        }

                        size_t remaining = file->size - target->offset;
                        size_t free_space = IOBuffer__free_space(&target->buffer);
                        size_t read_size = min(size, min(remaining, free_space));

                        Data buffer[PAGE_SIZE];
                        size_t copied = 0;
                        while (copied < read_size)
                        {
                            size_t chunk_size = min(PAGE_SIZE, read_size - copied);
                            ResultMatch(CollectionCode, MemFile__copy_from_file(file, target->offset + copied, buffer, chunk_size))
                                IsError(code, message, _cs_return Error(IOBufferPtr, message, code))
                            End
                            IOBuffer__push_all(&target->buffer, buffer, chunk_size);
                            copied += chunk_size;
                        }
                        target->offset += read_size;

                        _cs_return Success(IOBufferPtr, &target->buffer);
                    })
                End
            })
        End
    })

    return Error(IOBufferPtr, invalid_file_handle, IO_UNKNOWN_HANDLE);
}

Result(ByteCount) MemFileIO__write(IOStatePtr self, IOInternalHandle handle, DataPtr data, size_t size)
{
    MemFilesystemIOStatePtr fs_state = (MemFilesystemIOStatePtr) self;

    CriticalSection(fs_state->state.lock,
    {
        Match(MemFileHandlePtr, MemFileHandleMap__get(&fs_state->handles, handle))
            IsNone(_cs_return Error(ByteCount, invalid_file_handle, IO_UNKNOWN_HANDLE))
            IsSome(target,
            {
                if ((target->flags & IO_OPEN_WRITE) == 0)
                {
                    _cs_return Error(ByteCount, invalid_file_handle, IO_OPERATION_NOT_SUPPORTED);
                }

                LLFindResultMatch(MemFiles, MemFilesystem__find_file(fs_state, &target->path))
                    IsNone(_cs_return Error(ByteCount, KString__sprintf(&invalid_path_format, &target->path), IO_INVALID_PATH))
                    IsSome(file_match,
                    {
                        MemFilePtr file = file_match.value;
                        ResultMatch(CollectionCode, MemFile__ensure_capacity(file, target->offset + size))
                            IsError(code, message, _cs_return Error(ByteCount, message, code))
                        End

                        ResultMatch(CollectionCode, MemFile__copy_to_file(file, target->offset, data, size))
                            IsError(code, message, _cs_return Error(ByteCount, message, code))
                        End

                        target->offset += size;
                        if (target->offset > file->size)
                        {
                            file->size = target->offset;
                        }

                        _cs_return Success(ByteCount, size);
                    })
                End
            })
        End
    })

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

Result(FilesystemNodes) MemFileIO__list_nodes(IOStatePtr self, FilesystemNodePtr parent, KString path)
{
    MemFilesystemIOStatePtr fs_state = (MemFilesystemIOStatePtr) self;
    FilesystemNodes result = EmptyArray(FilesystemNodes);

    CriticalSection(fs_state->state.lock,
    {
        if (!MemFilesystem__directory_exists(fs_state, &path))
        {
            _cs_return Error(FilesystemNodes, KString__sprintf(&invalid_path_format, &path), IO_INVALID_PATH);
        }

        LLIterate(MemDirectories, fs_state->directories, directory)
            KString directory_parent = MemFilesystem__parent_path(directory.path);
            if (KString__equals(&directory_parent, &path))
            {
                FilesystemNodePtr node = FilesystemNode__heap_new(
                    fs_state->state.filesystem, MemFilesystem__basename(directory.path), 0
                );
                FilesystemNodes__push(&result, node);
            }
        End

        LLIterate(MemFiles, fs_state->files, file)
            KString file_parent = MemFilesystem__parent_path(file.name);
            if (KString__equals(&file_parent, &path))
            {
                FilesystemNodePtr node = FilesystemNode__heap_new(
                    fs_state->state.filesystem, MemFilesystem__basename(file.name), 0
                );
                FilesystemNodes__push(&result, node);
            }
        End
    })

    return Success(FilesystemNodes, result);
}

Result(IOStatus) MemFileIO__mkdir(IOStatePtr self, KString path)
{
    MemFilesystemIOStatePtr fs_state = (MemFilesystemIOStatePtr) self;
    KString directory_path = path;
    if (MemFilesystem__basename(directory_path).len == 0)
    {
        return Error(IOStatus, KString__sprintf(&invalid_path_format, &path), IO_INVALID_PATH);
    }

    CriticalSection(fs_state->state.lock,
    {
        KString parent = MemFilesystem__parent_path(directory_path);
        if (!MemFilesystem__directory_exists(fs_state, &parent))
        {
            _cs_return Error(IOStatus, KString__sprintf(&invalid_path_format, &path), IO_INVALID_PATH);
        }

        LLFindResultMatch(MemDirectories, MemFilesystem__find_directory(fs_state, &directory_path))
            IsSome(_, _cs_return Error(IOStatus, KString__sprintf(&invalid_path_format, &path), IO_INVALID_PATH))
        End

        LLFindResultMatch(MemFiles, MemFilesystem__find_file(fs_state, &directory_path))
            IsSome(_, _cs_return Error(IOStatus, KString__sprintf(&invalid_path_format, &path), IO_INVALID_PATH))
        End

        ResultMatch(CollectionCode, MemDirectories__push(&fs_state->directories, ((MemDirectory) { .path = directory_path })))
            IsSuccess(_, _cs_return Success(IOStatus, IO_OK))
            IsError(code, message, _cs_return Error(IOStatus, message, code))
        End
    })

    return Error(IOStatus, KString__sprintf(&invalid_path_format, &path), IO_INVALID_PATH);
}

Result(IOStatus) MemFileIO__remove(IOStatePtr self, KString path, Flags flags)
{
    MemFilesystemIOStatePtr fs_state = (MemFilesystemIOStatePtr) self;
    KString file_path = path;

    CriticalSection(fs_state->state.lock,
    {
        ResultMatch(IOStatus, MemFilesystem__remove_file(fs_state, &file_path))
            IsSuccess(status, _cs_return Success(IOStatus, status))
        End

        LLFindResultMatch(MemDirectories, MemFilesystem__find_directory(fs_state, &file_path))
            IsSome(_,
            {
                if ((flags & IO_REMOVE_RECURSIVE) != 0)
                {
                    ResultMatch(IOStatus, MemFilesystem__remove_recursive(fs_state, &file_path))
                        IsSuccess(status, _cs_return Success(IOStatus, status))
                        IsError(code, message, _cs_return Error(IOStatus, message, code))
                    End
                }

                if (MemFilesystem__has_child(fs_state, &file_path))
                {
                    _cs_return Error(IOStatus, directory_not_empty, IO_OPERATION_NOT_SUPPORTED);
                }
                _cs_return Error(IOStatus, kstring("remove directory not supported"), IO_OPERATION_NOT_SUPPORTED);
            })
        End
    })

    return Error(IOStatus, KString__sprintf(&invalid_path_format, &path), IO_INVALID_PATH);
}

Result(IOStatus) MemFileIO__link(IOStatePtr self, KString source_path, KString target_path)
{
    return Error(IOStatus, kstring("link not supported"), IO_OPERATION_NOT_SUPPORTED);
}

FileIO MemFilesystemIO__new()
{
    FileIO mem_file_io = (FileIO) {
        .base_io = (IO) {
            .read = MemFileIO__read,
            .write = MemFileIO__write,
            .open = MemFileIO__open,
            .close = MemFileIO__close,
            .park = MemFileIO__park,
            .unpark = MemFileIO__unpark,
        },
        .list_nodes = MemFileIO__list_nodes,
        .mkdir = MemFileIO__mkdir,
        .remove = MemFileIO__remove,
        .link = MemFileIO__link,
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
