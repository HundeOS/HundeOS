#include "filesystem_driver.h"
#include "sys/mem.h"
#include "sys/service.h"

FixedArrayBodyDefine(FilesystemFiles);

bool FilesystemFile__equals(void * first, void * second)
{
    return ((FilesystemFilePtr)first)->handle == ((FilesystemFilePtr)second)->handle;
}

FilesystemOperations FilesystemOperations__new_snapshot(FilesystemLoad load, FilesystemStore store, FilesystemList list,
                                                        FilesystemMkdir mkdir, FilesystemRemove remove,
                                                        FilesystemLink link, FilesystemFileSize write_capacity)
{
    return (FilesystemOperations){.source_mode = FILESYSTEM_SOURCE_SNAPSHOT,
                                  .source.snapshot = {.load = load},
                                  .store = store,
                                  .list = list,
                                  .mkdir = mkdir,
                                  .remove = remove,
                                  .link = link,
                                  .write_capacity = write_capacity};
}

FilesystemOperations FilesystemOperations__new_stream(FilesystemDescribe describe, FilesystemRead read,
                                                      FilesystemStore store, FilesystemList list, FilesystemMkdir mkdir,
                                                      FilesystemRemove remove, FilesystemLink link,
                                                      FilesystemFileSize write_capacity)
{
    return (FilesystemOperations){.source_mode = FILESYSTEM_SOURCE_STREAM,
                                  .source.stream = {.describe = describe, .read = read},
                                  .store = store,
                                  .list = list,
                                  .mkdir = mkdir,
                                  .remove = remove,
                                  .link = link,
                                  .write_capacity = write_capacity};
}

IOStatus FilesystemOperations__unsupported_link(IOServiceContext context, ConstBStringPtr source_path,
                                                ConstBStringPtr target_path)
{
    return IO_OPERATION_NOT_SUPPORTED;
}

FilesystemDriver FilesystemDriver__new(IOServiceContext volume, ConstFilesystemOperationsPtr operations)
{
    return (FilesystemDriver){.volume = volume,
                              .operations = *operations,
                              .files = FilesystemFiles__new(),
                              .next_handle = IO_SESSION_HANDLE_BASE};
}

static FilesystemFile FilesystemFile__new(IOHandle handle, IORequestPtr request, ConstBStringPtr path)
{
    return (FilesystemFile){.handle = handle,
                            .session = request->client_session,
                            .pid = request->client_pid,
                            .path = *path,
                            .flags = request->flags,
                            .data = None(FilesystemFileDataPtr)};
}

static bool FilesystemFile__matches(FilesystemFilePtr self, void * context)
{
    IORequestPtr request = context;
    return self->handle == request->handle && self->session == request->client_session &&
           self->pid == request->client_pid;
}

static Optional(FilesystemFilePtr) FilesystemDriver__find(FilesystemDriverPtr self, IORequestPtr request)
{
    match_find_result_ptr(
        FilesystemFiles__find(&self->files, FilesystemFile__matches, request),
        some(file)
        {
            return Some(FilesystemFilePtr, file);
        },
        none()
        {
            return None(FilesystemFilePtr);
        }
    );
}

static IOReply FilesystemDriver__open(FilesystemDriverPtr self, IORequestPtr request, ConstBStringPtr path)
{
    if (request->flags == 0 || (request->flags & ~(IO_OPEN_READ | IO_OPEN_WRITE | IO_OPEN_EXECUTE)) != 0)
    {
        return IOReply__new(IO_OPERATION_NOT_SUPPORTED, 0);
    }
    if ((request->flags & IO_OPEN_EXECUTE) != 0 && (request->flags & IO_OPEN_READ) == 0)
    {
        return IOReply__new(IO_PERMISSION_DENIED, 0);
    }
    if (FilesystemFiles__len(&self->files) == FilesystemFiles__max_size || self->next_handle == INT64_MAX)
    {
        return IOReply__new(IO_REGISTER_FAILED, 0);
    }
    FilesystemFile file = FilesystemFile__new(self->next_handle++, request, path);
    if (self->operations.source_mode == FILESYSTEM_SOURCE_SNAPSHOT)
    {
        if ((request->flags & IO_OPEN_EXECUTE) != 0)
        {
            return IOReply__new(IO_PERMISSION_DENIED, 0);
        }
        match_result(
            mem_alloc_bytes(FILESYSTEM_FILE_CAPACITY),
            success(memory)
            {
                FilesystemFileDataPtr data = (FilesystemFileDataPtr)memory;
                file.data = Some(FilesystemFileDataPtr, data);
                if ((request->flags & IO_OPEN_READ) != 0 || self->operations.write_capacity == 0)
                {
                    match_result(
                        self->operations.source.snapshot.load(self->volume, path, data),
                        success(size)
                        {
                            file.size = size;
                        },
                        error(code, message)
                        {
                            free(data);
                            return IOReply__new((IOStatus)code, 0);
                        }
                    );
                }
            },
            error(code, message)
            {
                return IOReply__new(IO_REGISTER_FAILED, 0);
            }
        );
        if (file.size > FILESYSTEM_FILE_CAPACITY)
        {
            match_optional(file.data,
                some(data)
                {
                    free(data);
                },
                none_do_nothing()
            );
            return IOReply__new(IO_UNKNOWN_BUFFER, 0);
        }
    }
    if (self->operations.source_mode == FILESYSTEM_SOURCE_STREAM &&
        (request->flags & (IO_OPEN_READ | IO_OPEN_EXECUTE)) != 0)
    {
        match_result(
            self->operations.source.stream.describe(self->volume, path),
            success(info)
            {
                if ((info.mode & IO_MODE_TYPE_MASK) != IO_MODE_TYPE_FILE)
                {
                    return IOReply__new(IO_INVALID_PATH, 0);
                }
                file.size = info.size;
                if ((request->flags & IO_OPEN_EXECUTE) != 0 && (info.mode & 0111) == 0)
                {
                    return IOReply__new(IO_PERMISSION_DENIED, 0);
                }
            },
            error(code, message)
            {
                return IOReply__new((IOStatus)code, 0);
            }
        );
    }
    if (self->operations.write_capacity == 0 && (request->flags & IO_OPEN_WRITE) != 0)
    {
        match_optional(file.data,
            some(data)
            {
                free(data);
            },
            none_do_nothing()
        );
        return IOReply__new(IO_PERMISSION_DENIED, 0);
    }
    if (self->operations.source_mode == FILESYSTEM_SOURCE_STREAM && (request->flags & IO_OPEN_WRITE) != 0)
    {
        if (file.size > self->operations.write_capacity)
        {
            return IOReply__new(IO_OPERATION_NOT_SUPPORTED, 0);
        }
        match_result(
            mem_alloc_bytes(self->operations.write_capacity),
            success(memory)
            {
                FilesystemFileDataPtr data = (FilesystemFileDataPtr)memory;
                file.data = Some(FilesystemFileDataPtr, data);
                if ((request->flags & IO_OPEN_READ) != 0 && file.size > 0)
                {
                    match_result(
                        self->operations.source.stream.read(self->volume, path, 0, data, file.size),
                        success(size)
                        {
                            if (size != file.size)
                            {
                                free(data);
                                return IOReply__new(IO_UNKNOWN_BUFFER, 0);
                            }
                        },
                        error(code, message)
                        {
                            free(data);
                            return IOReply__new((IOStatus)code, 0);
                        }
                    );
                }
            },
            error(code, message)
            {
                return IOReply__new(IO_REGISTER_FAILED, 0);
            }
        );
    }
    match_result(
        FilesystemFiles__push(&self->files, file),
        success(code)
        {
            return IOReply__new(IO_OK, file.handle);
        },
        error(code, message)
        {
            match_optional(file.data,
                some(data)
                {
                    free(data);
                },
                none_do_nothing()
            );
            return IOReply__new(IO_REGISTER_FAILED, 0);
        }
    );
}

static IOReply FilesystemDriver__transfer(FilesystemDriverPtr self, FilesystemFilePtr file, IORequestPtr request)
{
    IOReply reply = IOReply__new(IO_OK, 0);
    Flags required = request->operation == IO_REQUEST_READ ? IO_OPEN_READ : IO_OPEN_WRITE;
    if ((file->flags & required) == 0)
    {
        return IOReply__new(IO_PERMISSION_DENIED, 0);
    }
    if (request->count == 0)
    {
        return reply;
    }
    if (request->operation == IO_REQUEST_READ)
    {
        if (file->offset >= file->size)
        {
            return reply;
        }
        uint64_t count = file->size - file->offset;
        if (count > request->count)
        {
            count = request->count;
        }
        if (self->operations.source_mode == FILESYSTEM_SOURCE_SNAPSHOT)
        {
            match_optional(
                file->data,
                some(data)
                {
                    __builtin_memcpy(IORequest__read_target(request, &reply), data + file->offset, count);
                    file->offset += count;
                    reply.value = count;
                    return reply;
                },
                none()
                {
                    return IOReply__new(IO_UNKNOWN_BUFFER, 0);
                }
            );
        }
        match_result(
            self->operations.source.stream.read(self->volume, &file->path, file->offset,
                                                IORequest__read_target(request, &reply), count),
            success(bytes)
            {
                if (bytes > count)
                {
                    return IOReply__new(IO_UNKNOWN_BUFFER, 0);
                }
                file->offset += bytes;
                reply.value = bytes;
                return reply;
            },
            error(code, message)
            {
                return IOReply__new((IOStatus)code, 0);
            }
        );
    }
    if (file->offset > self->operations.write_capacity ||
        request->count > self->operations.write_capacity - file->offset)
    {
        return IOReply__new(IO_OPERATION_NOT_SUPPORTED, 0);
    }
    FilesystemFileDataPtr data;
    match_optional(file->data,
        some(value)
        {
            data = value;
        },
        none()
        {
            return IOReply__new(IO_UNKNOWN_BUFFER, 0);
        }
    );
    ChannelDataPtr previous;
    match_result(
        mem_alloc_bytes(request->count),
        success(memory)
        {
            previous = (ChannelDataPtr)memory;
        },
        error(code, message)
        {
            return IOReply__new(IO_UNKNOWN_BUFFER, 0);
        }
    );
    __builtin_memcpy(previous, data + file->offset, request->count);
    __builtin_memcpy(data + file->offset, IORequest__write_source(request), request->count);
    uint64_t size = file->offset + request->count;
    if (size < file->size)
    {
        size = file->size;
    }
    IOStatus status = self->operations.store(self->volume, &file->path, data, size);
    if (status != IO_OK)
    {
        __builtin_memcpy(data + file->offset, previous, request->count);
        free(previous);
        return IOReply__new(status, 0);
    }
    file->offset += request->count;
    free(previous);
    file->size = size;
    return IOReply__new(IO_OK, request->count);
}

static IOReply FilesystemDriver__list(FilesystemDriverPtr self, ConstBStringPtr path, uint64_t offset)
{
    IOReply reply = IOReply__new(IO_OK, 0);
    match_result(
        self->operations.list(self->volume, path, reply.directory.entries, IO_DIRECTORY_BATCH_CAPACITY, offset),
        success(count)
        {
            if (count < 0 || (uint64_t)count > IO_DIRECTORY_BATCH_CAPACITY)
            {
                return IOReply__new(IO_UNKNOWN_BUFFER, 0);
            }
            reply.directory.count = (uint64_t)count;
            return reply;
        },
        error(code, message)
        {
            return IOReply__new((IOStatus)code, 0);
        }
    );
}

static BString FilesystemDriver__parent(ConstBStringPtr path)
{
    StringPos end = BString__length(path);
    while (end > 1 && BString__char_at_or(path, end - 1, 0) != '/')
    {
        end--;
    }
    return BString__slice(path, 0, end > 1 ? end - 1 : 1);
}

static BString FilesystemDriver__name(ConstBStringPtr path)
{
    StringPos start = BString__length(path);
    while (start > 0 && BString__char_at_or(path, start - 1, 0) != '/')
    {
        start--;
    }
    return BString__slice(path, start, BString__length(path));
}

static IOReply FilesystemDriver__file_info(FilesystemDriverPtr self, ConstBStringPtr path)
{
    if (self->operations.source_mode == FILESYSTEM_SOURCE_STREAM)
    {
        match_result(
            self->operations.source.stream.describe(self->volume, path),
            success(info)
            {
                IOReply reply = IOReply__new(IO_OK, 0);
                reply.file_info = IOFileInfo__new(info.mode, PROCESS_USER_ROOT, PROCESS_GROUP_ROOT, info.size);
                return reply;
            },
            error(code, message)
            {
                return IOReply__new((IOStatus)code, 0);
            }
        );
    }
    DirectoryEntry probe;
    match_result(
        self->operations.list(self->volume, path, &probe, 1, 0),
        success(count)
        {
            if (count < 0 || count > 1)
            {
                return IOReply__new(IO_UNKNOWN_BUFFER, 0);
            }
            IOReply reply = IOReply__new(IO_OK, 0);
            reply.file_info = IOFileInfo__new(IO_MODE_TYPE_DIR | 0755, PROCESS_USER_ROOT, PROCESS_GROUP_ROOT, 0);
            return reply;
        },
        error(code, message)
        {
            if (code != IO_INVALID_PATH)
            {
                return IOReply__new((IOStatus)code, 0);
            }
        }
    );
    BString parent = FilesystemDriver__parent(path);
    BString name = FilesystemDriver__name(path);
    uint64_t offset = 0;
    while (offset != UINT64_MAX)
    {
        DirectoryEntry entries[IO_DIRECTORY_BATCH_CAPACITY];
        match_result(
            self->operations.list(self->volume, &parent, entries, IO_DIRECTORY_BATCH_CAPACITY, offset),
            success(count)
            {
                if (count < 0 || (uint64_t)count > IO_DIRECTORY_BATCH_CAPACITY)
                {
                    return IOReply__new(IO_UNKNOWN_BUFFER, 0);
                }
                if (count == 0)
                {
                    return IOReply__new(IO_INVALID_PATH, 0);
                }
                for (DirectoryEntryCount index = 0; index < count; index++)
                {
                    BString entry_name = BString__from_chars(entries[index].name, entries[index].name_len);
                    if (BString__equals(&entry_name, &name))
                    {
                        IOReply reply = IOReply__new(IO_OK, 0);
                        reply.file_info =
                            IOFileInfo__new(entries[index].mode, entries[index].user_id, entries[index].group_id, 0);
                        return reply;
                    }
                }
                if ((uint64_t)count > UINT64_MAX - offset)
                {
                    return IOReply__new(IO_UNKNOWN_BUFFER, 0);
                }
                offset += (uint64_t)count;
            },
            error(code, message)
            {
                return IOReply__new((IOStatus)code, 0);
            }
        );
    }
    return IOReply__new(IO_INVALID_PATH, 0);
}

IOReply FilesystemDriver__execute(FilesystemDriverPtr self, IORequestPtr request)
{
    if (request->client_pid == 0 || request->client_session == 0)
    {
        return IOReply__new(IO_PERMISSION_DENIED, 0);
    }
    if (request->count > IORequest__capacity(request))
    {
        return IOReply__new(IO_UNKNOWN_BUFFER, 0);
    }
    if (request->operation == IO_REQUEST_RELEASE_CLIENT)
    {
        return IOReply__new(
            FilesystemDriver__release(self, request->client_session, request->client_pid) ? IO_OK : IO_UNKNOWN_HANDLE,
            0);
    }
    BString path = IORequest__path(request);
    switch (request->operation)
    {
    case IO_REQUEST_OPEN:
        return FilesystemDriver__open(self, request, &path);
    case IO_REQUEST_MKDIR:
        return IOReply__new(self->operations.mkdir(self->volume, &path), 0);
    case IO_REQUEST_REMOVE:
        return IOReply__new(self->operations.remove(self->volume, &path, request->flags), 0);
    case IO_REQUEST_LINK:
    {
        BString target_path = IORequest__target_path(request);
        return IOReply__new(self->operations.link(self->volume, &path, &target_path), 0);
    }
    case IO_REQUEST_LIST_DIR:
        return FilesystemDriver__list(self, &path, request->offset);
    case IO_REQUEST_FILE_INFO:
        return FilesystemDriver__file_info(self, &path);
    case IO_REQUEST_READ:
    case IO_REQUEST_WRITE:
    case IO_REQUEST_CLOSE:
        break;
    default:
        return IOReply__new(IO_OPERATION_NOT_SUPPORTED, 0);
    }
    match_optional(
        FilesystemDriver__find(self, request),
        some(file)
        {
            if (request->operation != IO_REQUEST_CLOSE)
            {
                return FilesystemDriver__transfer(self, file, request);
            }
            FilesystemFile removed = *file;
            match_result(
                FilesystemFiles__remove(&self->files, removed),
                success(code)
                {
                    match_optional(removed.data,
                        some(data)
                        {
                            free(data);
                        },
                        none_do_nothing()
                    );
                    return IOReply__new(IO_OK, 0);
                },
                error(code, message)
                {
                    return IOReply__new(IO_UNKNOWN_HANDLE, 0);
                }
            );
        },
        none()
        {
            return IOReply__new(IO_UNKNOWN_HANDLE, 0);
        }
    );
}

static bool FilesystemFile__owned(FilesystemFilePtr self, void * context)
{
    IORequestPtr owner = context;
    return self->session == owner->client_session && self->pid == owner->client_pid;
}

bool FilesystemDriver__release(FilesystemDriverPtr self, QueueConnectionID session, uint64_t pid)
{
    IORequest owner = IORequest__new(IO_REQUEST_RELEASE_CLIENT, 0);
    owner.client_session = session;
    owner.client_pid = pid;
    while (true)
    {
        match_find_result(
            FilesystemFiles__find(&self->files, FilesystemFile__owned, &owner),
            some(file)
            {
                match_result(
                    FilesystemFiles__remove(&self->files, file),
                    success(code)
                    {
                        match_optional(file.data,
                        some(data)
                        {
                            free(data);
                        },
                        none_do_nothing()
                    );
                    },
                    error(code, message)
                    {
                        return false;
                    }
                );
            },
            none()
            {
                return true;
            }
        );
    }
}
