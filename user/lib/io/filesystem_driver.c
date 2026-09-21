#include "filesystem_driver.h"
#include "sys/mem.h"
#include "sys/service.h"

FixedArrayBodyDefine(FilesystemFiles);

bool FilesystemFile__equals(void * first, void * second)
{
    return ((FilesystemFilePtr)first)->handle == ((FilesystemFilePtr)second)->handle;
}

FilesystemOperations FilesystemOperations__new(FilesystemLoad load, FilesystemStore store, FilesystemList list,
                                               FilesystemMkdir mkdir, FilesystemRemove remove,
                                               FilesystemFileSize write_capacity)
{
    return (FilesystemOperations){
        .load = load, .store = store, .list = list, .mkdir = mkdir, .remove = remove, .write_capacity = write_capacity};
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
                            .flags = request->flags};
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
    match_result(
        mem_alloc_bytes(FILESYSTEM_FILE_CAPACITY),
        success(memory)
        {
            file.data = (FilesystemFileDataPtr)memory;
        },
        error(code, message)
        {
            return IOReply__new(IO_REGISTER_FAILED, 0);
        }
    );
    if ((request->flags & (IO_OPEN_READ | IO_OPEN_EXECUTE)) != 0 || self->operations.write_capacity == 0)
    {
        match_result(
            self->operations.load(self->volume, path, file.data),
            success(size)
            {
                file.size = size;
            },
            error(code, message)
            {
                free(file.data);
                return IOReply__new((IOStatus)code, 0);
            }
        );
    }
    if (self->operations.write_capacity == 0 && (request->flags & IO_OPEN_WRITE) != 0)
    {
        free(file.data);
        return IOReply__new(IO_PERMISSION_DENIED, 0);
    }
    if (file.size > FILESYSTEM_FILE_CAPACITY)
    {
        free(file.data);
        return IOReply__new(IO_UNKNOWN_BUFFER, 0);
    }
    match_result(
        FilesystemFiles__push(&self->files, file),
        success(code)
        {
            return IOReply__new(IO_OK, file.handle);
        },
        error(code, message)
        {
            free(file.data);
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
        uint64_t count = file->size - file->offset;
        if (count > request->count)
        {
            count = request->count;
        }
        __builtin_memcpy(IORequest__read_target(request, &reply), file->data + file->offset, count);
        file->offset += count;
        reply.value = count;
        return reply;
    }
    if (file->offset > self->operations.write_capacity ||
        request->count > self->operations.write_capacity - file->offset)
    {
        return IOReply__new(IO_OPERATION_NOT_SUPPORTED, 0);
    }
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
    __builtin_memcpy(previous, file->data + file->offset, request->count);
    __builtin_memcpy(file->data + file->offset, IORequest__write_source(request), request->count);
    uint64_t size = file->offset + request->count;
    if (size < file->size)
    {
        size = file->size;
    }
    IOStatus status = self->operations.store(self->volume, &file->path, file->data, size);
    if (status != IO_OK)
    {
        __builtin_memcpy(file->data + file->offset, previous, request->count);
        free(previous);
        return IOReply__new(status, 0);
    }
    file->offset += request->count;
    free(previous);
    file->size = size;
    return IOReply__new(IO_OK, request->count);
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
    case IO_REQUEST_LIST_DIR:
        match_result(
            self->operations.list(self->volume, &path, request->offset),
            success(entry)
            {
                IOReply reply = IOReply__new(IO_OK, 0);
                match_optional(
                    entry,
                    some(value)
                    {
                        reply.entry = value;
                        reply.value = 1;
                    },
                    none_do_nothing()
                );
                return reply;
            },
            error(code, message)
            {
                return IOReply__new((IOStatus)code, 0);
            }
        );
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
                    free(removed.data);
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
                        free(file.data);
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
