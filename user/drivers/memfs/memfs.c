#include "memfs.h"
#include "sys/mem.h"
#include "sys/service.h"
#include "sys/process.h"

LinkedListBodyDefine(MemfsNodes);
FixedArrayBodyDefine(MemfsHandles);

bool MemfsNode__equals(void * first, void * second)
{
    return ((MemfsNodePtr)first)->id == ((MemfsNodePtr)second)->id;
}

bool MemfsHandle__equals(void * first, void * second)
{
    return ((MemfsHandlePtr)first)->id == ((MemfsHandlePtr)second)->id;
}

Memfs Memfs__new(void)
{
    return (Memfs){.nodes = MemfsNodes__new(),
                   .handles = MemfsHandles__new(),
                   .next_node = 1,
                   .next_handle = IO_SESSION_HANDLE_BASE};
}

static void MemfsNode__truncate(MemfsNodePtr self)
{
    match_optional(self->data,
        some(data)
        {
            free(data);
        },
        none_do_nothing()
    );
    self->data = None(MemfsDataPtr);
    self->size = self->capacity = 0;
}

void Memfs__dispose(MemfsPtr self)
{
    while (MemfsNodes__len(&self->nodes) != 0)
    {
        match_optional(
            MemfsNodes__remove_at(&self->nodes, 0),
            some(node)
            {
                MemfsNode__truncate(&node);
            },
            none()
            {
                break;
            }
        );
    }
    self->handles = MemfsHandles__new();
}

static bool MemfsNode__path_matches(MemfsNodePtr self, void * context)
{
    return BString__equals(&self->path, context);
}

static bool MemfsNode__id_matches(MemfsNodePtr self, void * context)
{
    MemfsHandlePtr handle = context;
    return self->id == handle->node;
}

static Optional(MemfsNodePtr) Memfs__find(MemfsPtr self, ConstBStringPtr path)
{
    match_find_result_ptr(
        MemfsNodes__find(&self->nodes, MemfsNode__path_matches, (void *)path),
        some(node)
        {
            return Some(MemfsNodePtr, node);
        },
        none()
        {
            return None(MemfsNodePtr);
        }
    );
}

static bool Memfs__directory(MemfsPtr self, ConstBStringPtr path)
{
    if (BString__equals(path, &bstr("/")))
    {
        return true;
    }
    match_optional(Memfs__find(self, path),
        some(node)
        {
            return node->directory;
        },
        none()
        {
            return false;
        }
    );
}

static BString Memfs__parent(ConstBStringPtr path)
{
    size_t end = BString__length(path);
    while (end > 1 && BString__char_at_or(path, end - 1, 0) != '/')
    {
        end--;
    }
    return BString__slice(path, 0, end > 1 ? end - 1 : 1);
}

static bool Memfs__valid_path(ConstBStringPtr path)
{
    if (BString__char_at_or(path, 0, 0) != '/' || BString__contains_char(path, '\0'))
    {
        return false;
    }
    if (BString__equals(path, &bstr("/")))
    {
        return true;
    }
    BString rest = BString__slice(path, 1, BString__length(path));
    if (BString__char_at_or(path, BString__length(path) - 1, 0) == '/')
    {
        return false;
    }
    while (!BString__is_empty(&rest))
    {
        BStringSplit split = BString__split_first_any(&rest, &bstr("/"));
        BString part = unwrap_optional(split.match, rest);
        if (BString__is_empty(&part) || BString__equals(&part, &bstr(".")) || BString__equals(&part, &bstr("..")))
        {
            return false;
        }
        rest = split.rest;
    }
    return true;
}

typedef enum MemfsAccess
{
    MEMFS_SEARCH = 1,
    MEMFS_WRITE = 2,
    MEMFS_READ = 4,
} MemfsAccess;

static bool MemfsNode__allows(MemfsNodePtr self, ConstProcessCredentialsPtr credentials, unsigned required)
{
    if (credentials->user_id == PROCESS_USER_ROOT)
    {
        return self->directory || (required & MEMFS_SEARCH) == 0 || (self->mode & 0111) != 0;
    }
    unsigned shift = credentials->user_id == self->owner.user_id     ? 6
                     : credentials->group_id == self->owner.group_id ? 3
                                                                     : 0;
    return ((self->mode >> shift) & required) == required;
}

static IOStatus Memfs__directory_access(MemfsPtr self, ConstBStringPtr path, ConstProcessCredentialsPtr credentials,
                                        unsigned required)
{
    if (BString__equals(path, &bstr("/")))
    {
        return IO_OK;
    }
    match_optional(
        Memfs__find(self, path),
        some(node)
        {
            if (!node->directory)
            {
                return IO_INVALID_PATH;
            }
            return MemfsNode__allows(node, credentials, required) ? IO_OK : IO_PERMISSION_DENIED;
        },
        none()
        {
            return IO_INVALID_PATH;
        }
    );
}

static IOStatus Memfs__search_parents(MemfsPtr self, ConstBStringPtr path, ConstProcessCredentialsPtr credentials)
{
    BString parent = Memfs__parent(path);
    while (!BString__equals(&parent, &bstr("/")))
    {
        IOStatus status = Memfs__directory_access(self, &parent, credentials, MEMFS_SEARCH);
        if (status != IO_OK)
        {
            return status;
        }
        parent = Memfs__parent(&parent);
    }
    return IO_OK;
}

static MemfsNode MemfsNode__new(MemfsNodeID id, ConstBStringPtr path, bool directory,
                                ConstProcessCredentialsPtr credentials)
{
    return (MemfsNode){.id = id,
                       .path = *path,
                       .directory = directory,
                       .owner = *credentials,
                       .mode = directory ? 0755 : 0644,
                       .data = None(MemfsDataPtr)};
}

static MemfsHandle MemfsHandle__new(IOHandle id, MemfsNodeID node, IORequestPtr request)
{
    return (MemfsHandle){.id = id,
                         .node = node,
                         .session = request->client_session,
                         .pid = request->client_pid,
                         .flags = request->flags};
}

static IOStatus Memfs__create(MemfsPtr self, ConstBStringPtr path, bool directory,
                              ConstProcessCredentialsPtr credentials)
{
    BString parent = Memfs__parent(path);
    IOStatus access = Memfs__directory_access(self, &parent, credentials, MEMFS_WRITE | MEMFS_SEARCH);
    if (access != IO_OK)
    {
        return access;
    }
    if (!Memfs__directory(self, &parent) || self->next_node == UINT64_MAX)
    {
        return IO_INVALID_PATH;
    }
    MemfsNode node = MemfsNode__new(self->next_node++, path, directory, credentials);
    match_result(
        MemfsNodes__push(&self->nodes, node),
        success(code)
        {
            return IO_OK;
        },
        error(code, message)
        {
            return IO_REGISTER_FAILED;
        }
    );
}

static IOReply Memfs__open(MemfsPtr self, IORequestPtr request, ConstBStringPtr path,
                           ConstProcessCredentialsPtr credentials)
{
    Flags access_flags = request->flags & (IO_OPEN_READ | IO_OPEN_WRITE | IO_OPEN_EXECUTE);
    Flags creation_flags = request->flags & (IO_OPEN_CREATE | IO_OPEN_EXCLUSIVE);
    if (access_flags == 0 ||
        (request->flags & ~(IO_OPEN_READ | IO_OPEN_WRITE | IO_OPEN_EXECUTE | IO_OPEN_CREATE | IO_OPEN_EXCLUSIVE)) !=
            0 ||
        (creation_flags != 0 && ((access_flags & IO_OPEN_WRITE) == 0 || (creation_flags & IO_OPEN_CREATE) == 0)))
    {
        return IOReply__new(IO_OPERATION_NOT_SUPPORTED, 0);
    }
    if (MemfsHandles__len(&self->handles) == MemfsHandles__max_size || self->next_handle == INT64_MAX)
    {
        return IOReply__new(IO_REGISTER_FAILED, 0);
    }
    if (Memfs__directory(self, path))
    {
        return IOReply__new(IO_INVALID_PATH, 0);
    }
    match_optional(
        Memfs__find(self, path),
        some(node)
        {
            if ((request->flags & IO_OPEN_EXCLUSIVE) != 0)
            {
                return IOReply__new(IO_ALREADY_EXISTS, 0);
            }
        },
        none()
        {
            if ((request->flags & IO_OPEN_WRITE) == 0)
            {
                return IOReply__new(IO_INVALID_PATH, 0);
            }
            if ((request->flags & IO_OPEN_EXECUTE) != 0)
            {
                return IOReply__new(IO_PERMISSION_DENIED, 0);
            }
            IOStatus status = Memfs__create(self, path, false, credentials);
            if (status != IO_OK)
            {
                return IOReply__new(status, 0);
            }
        }
    );
    match_optional(
        Memfs__find(self, path),
        some(node)
        {
            unsigned required = ((request->flags & IO_OPEN_READ) != 0 ? MEMFS_READ : 0) |
                                ((request->flags & IO_OPEN_WRITE) != 0 ? MEMFS_WRITE : 0) |
                                ((request->flags & IO_OPEN_EXECUTE) != 0 ? MEMFS_SEARCH : 0);
            if (!MemfsNode__allows(node, credentials, required))
            {
                return IOReply__new(IO_PERMISSION_DENIED, 0);
            }
            MemfsHandle handle = MemfsHandle__new(self->next_handle++, node->id, request);
            match_result(
                MemfsHandles__push(&self->handles, handle),
                success(code)
                {
                    if (request->flags == IO_OPEN_WRITE)
                    {
                        MemfsNode__truncate(node);
                    }
                    return IOReply__new(IO_OK, handle.id);
                },
                error(code, message)
                {
                    return IOReply__new(IO_REGISTER_FAILED, 0);
                }
            );
        },
        none()
        {
            return IOReply__new(IO_INVALID_PATH, 0);
        }
    );
}

static IOStatus MemfsNode__reserve(MemfsNodePtr self, size_t required)
{
    if (required <= self->capacity)
    {
        return IO_OK;
    }
    size_t capacity = self->capacity == 0 ? 4096 : self->capacity;
    while (capacity < required)
    {
        if (capacity > SIZE_MAX / 2)
        {
            return IO_REGISTER_FAILED;
        }
        capacity *= 2;
    }
    match_result(
        mem_alloc_bytes(capacity),
        success(memory)
        {
            match_optional(
                self->data,
                some(previous)
                {
                    __builtin_memcpy((MemfsDataPtr)memory, previous, self->size);
                    free(previous);
                },
                none_do_nothing()
            );
            self->data = Some(MemfsDataPtr, (MemfsDataPtr)memory);
            self->capacity = capacity;
            return IO_OK;
        },
        error(code, message)
        {
            return IO_REGISTER_FAILED;
        }
    );
}

static IOReply MemfsNode__transfer(MemfsNodePtr self, MemfsHandlePtr handle, IORequestPtr request)
{
    bool writing = request->operation == IO_REQUEST_WRITE;
    Flags supported_flags = IO_TRANSFER_NONBLOCK | (writing ? IO_TRANSFER_APPEND : 0);
    if ((request->flags & ~supported_flags) != 0)
    {
        return IOReply__new(IO_OPERATION_NOT_SUPPORTED, 0);
    }
    if ((handle->flags & (writing ? IO_OPEN_WRITE : IO_OPEN_READ)) == 0)
    {
        return IOReply__new(IO_PERMISSION_DENIED, 0);
    }
    size_t count = request->count;
    size_t transfer_offset = writing && (request->flags & IO_TRANSFER_APPEND) != 0 ? self->size : handle->offset;
    if (!writing)
    {
        size_t available = transfer_offset < self->size ? self->size - transfer_offset : 0;
        if (count > available)
        {
            count = available;
        }
    }
    if (count == 0)
    {
        return IOReply__new(IO_OK, 0);
    }
    if (writing)
    {
        if (transfer_offset > SIZE_MAX - count)
        {
            return IOReply__new(IO_UNKNOWN_BUFFER, 0);
        }
        IOStatus status = MemfsNode__reserve(self, transfer_offset + count);
        if (status != IO_OK)
        {
            return IOReply__new(status, 0);
        }
    }
    match_optional(
        self->data,
        some(data)
        {
            IOReply reply = IOReply__new(IO_OK, count);
            if (writing)
            {
                if (transfer_offset > self->size)
                {
                    __builtin_memset(data + self->size, 0, transfer_offset - self->size);
                }
                __builtin_memcpy(data + transfer_offset, IORequest__write_source(request), count);
                if (self->size < transfer_offset + count)
                {
                    self->size = transfer_offset + count;
                }
            }
            else
            {
                __builtin_memcpy(IORequest__read_target(request, &reply), data + transfer_offset, count);
            }
            handle->offset = transfer_offset + count;
            return reply;
        },
        none()
        {
            return IOReply__new(IO_UNKNOWN_BUFFER, 0);
        }
    );
}

static IOReply MemfsNode__seek(MemfsNodePtr self, MemfsHandlePtr handle, IORequestPtr request)
{
    int64_t base;
    switch ((IOSeekWhence)request->flags)
    {
    case IO_SEEK_SET:
        base = 0;
        break;
    case IO_SEEK_CURRENT:
        base = (int64_t)handle->offset;
        break;
    case IO_SEEK_END:
        base = (int64_t)self->size;
        break;
    default:
        return IOReply__new(IO_OPERATION_NOT_SUPPORTED, 0);
    }
    int64_t delta = (int64_t)request->offset;
    if ((delta < 0 && (delta == INT64_MIN || base < -delta)) || (delta > 0 && base > INT64_MAX - delta))
    {
        return IOReply__new(IO_OPERATION_NOT_SUPPORTED, 0);
    }
    int64_t position = base + delta;
    if (position < 0)
    {
        return IOReply__new(IO_OPERATION_NOT_SUPPORTED, 0);
    }
    handle->offset = (size_t)position;
    return IOReply__new(IO_OK, (Anything)handle->offset);
}

static bool MemfsHandle__matches(MemfsHandlePtr self, void * context)
{
    IORequestPtr request = context;
    return self->id == request->handle && self->pid == request->client_pid && self->session == request->client_session;
}

static bool MemfsHandle__owned(MemfsHandlePtr self, void * context)
{
    IORequestPtr request = context;
    return self->pid == request->client_pid && self->session == request->client_session;
}

static IOReply Memfs__release(MemfsPtr self, IORequestPtr request)
{
    while (true)
    {
        match_find_result(
            MemfsHandles__find(&self->handles, MemfsHandle__owned, request),
            some(handle)
            {
                match_result(
                    MemfsHandles__remove(&self->handles, handle),
                    success_do_nothing(),
                    error(code, message)
                    {
                        return IOReply__new(IO_UNKNOWN_HANDLE, 0);
                    }
                );
            },
            none()
            {
                return IOReply__new(IO_OK, 0);
            }
        );
    }
}

static IOReply Memfs__list(MemfsPtr self, ConstBStringPtr path, uint64_t index, ConstProcessCredentialsPtr credentials)
{
    IOStatus access = Memfs__directory_access(self, path, credentials, MEMFS_READ | MEMFS_SEARCH);
    if (access != IO_OK)
    {
        return IOReply__new(access, 0);
    }
    IOReply reply = IOReply__new(IO_OK, 0);
    for_each_ll_ptr(
        MemfsNodes,
        (&self->nodes),
        node,
        {
            BString parent = Memfs__parent(&node.path);
            if (!BString__equals(&parent, path))
            {
                continue;
            }
            if (index > 0)
            {
                index--;
                continue;
            }
            size_t start = BString__equals(path, &bstr("/")) ? 1 : BString__length(path) + 1;
            BString name = BString__slice(&node.path, start, BString__length(&node.path));
reply.directory.entries[reply.directory.count++] = DirectoryEntry__new(
IO_DIRENT_ATTR_NONE, node.owner.user_id, node.owner.group_id,
(node.directory ? IO_MODE_TYPE_DIR : IO_MODE_TYPE_FILE) | node.mode, &name);
if (reply.directory.count == IO_DIRECTORY_BATCH_CAPACITY)
{
return reply;
}
        }
    );
    return reply;
}

static IOReply Memfs__file_info(MemfsPtr self, ConstBStringPtr path)
{
    IOReply reply = IOReply__new(IO_OK, 0);
    if (BString__equals(path, &bstr("/")))
    {
        reply.file_info = IOFileInfo__new(IO_MODE_TYPE_DIR | 0755, PROCESS_USER_ROOT, PROCESS_GROUP_ROOT, 0);
        return reply;
    }
    match_optional(
        Memfs__find(self, path),
        some(node)
        {
            reply.file_info = IOFileInfo__new((node->directory ? IO_MODE_TYPE_DIR : IO_MODE_TYPE_FILE) | node->mode,
                                              node->owner.user_id, node->owner.group_id, node->size);
            return reply;
        },
        none()
        {
            return IOReply__new(IO_INVALID_PATH, 0);
        }
    );
}

static bool MemfsNode__descendant(MemfsNodePtr self, void * context)
{
    ConstBStringPtr path = context;
    return BString__starts_with(&self->path, path) && BString__char_at_or(&self->path, BString__length(path), 0) == '/';
}

static IOStatus Memfs__remove_node(MemfsPtr self, MemfsNode node)
{
    match_result(
        MemfsNodes__remove(&self->nodes, node),
        success(code)
        {
            MemfsNode__truncate(&node);
            return IO_OK;
        },
        error(code, message)
        {
            return IO_INVALID_PATH;
        }
    );
}

static IOStatus Memfs__can_remove(MemfsPtr self, MemfsNodePtr node, ConstProcessCredentialsPtr credentials)
{
    BString parent = Memfs__parent(&node->path);
    IOStatus access = Memfs__directory_access(self, &parent, credentials, MEMFS_WRITE | MEMFS_SEARCH);
    if (access != IO_OK)
    {
        return access;
    }
    // The mount root has shared, sticky /tmp semantics.
    if (BString__equals(&parent, &bstr("/")) && credentials->user_id != node->owner.user_id &&
        credentials->user_id != PROCESS_USER_ROOT)
    {
        return IO_PERMISSION_DENIED;
    }

    return IO_OK;
}

static IOReply Memfs__remove(MemfsPtr self, ConstBStringPtr path, Flags flags, ConstProcessCredentialsPtr credentials)
{
    if ((flags & ~IO_REMOVE_RECURSIVE) != 0 || BString__equals(path, &bstr("/")))
    {
        return IOReply__new(IO_OPERATION_NOT_SUPPORTED, 0);
    }
    match_optional(
        Memfs__find(self, path),
        some(node)
        {
            MemfsNode removed = *node;
            IOStatus access = Memfs__can_remove(self, node, credentials);
            if (access != IO_OK)
            {
                return IOReply__new(access, 0);
            }
            if (node->directory && (flags & IO_REMOVE_RECURSIVE) == 0)
            {
                return IOReply__new(IO_OPERATION_NOT_SUPPORTED, 0);
            }
            // Check the complete tree before freeing anything, so denial cannot partially remove it.
            for_each_ll_ptr(
                MemfsNodes,
                (&self->nodes),
                child,
                {
                    if (MemfsNode__descendant(&child, (void *)path))
                    {
                        IOStatus child_access = Memfs__can_remove(self, &child, credentials);
                        if (child_access != IO_OK)
                        {
                            return IOReply__new(child_access, 0);
                        }
                    }
                }
            );
            while (true)
            {
                match_find_result(
                    MemfsNodes__find(&self->nodes, MemfsNode__descendant, (void *)path),
                    some(child)
                    {
                        IOStatus status = Memfs__remove_node(self, child);
                        if (status != IO_OK)
                        {
                            return IOReply__new(status, 0);
                        }
                    },
                    none()
                    {
                        break;
                    }
                );
            }
            return IOReply__new(Memfs__remove_node(self, removed), 0);
        },
        none()
        {
            return IOReply__new(IO_INVALID_PATH, 0);
        }
    );
}

IOReply Memfs__execute(MemfsPtr self, IORequestPtr request)
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
        return Memfs__release(self, request);
    }

    if (request->operation == IO_REQUEST_OPEN || request->operation == IO_REQUEST_LIST_DIR ||
        request->operation == IO_REQUEST_FILE_INFO || request->operation == IO_REQUEST_MKDIR ||
        request->operation == IO_REQUEST_REMOVE)
    {
        BString path = IORequest__path(request);
        if (BString__is_empty(&path))
        {
            path = bstr("/");
        }
        if (!Memfs__valid_path(&path))
        {
            return IOReply__new(IO_INVALID_PATH, 0);
        }
        ProcessCredentials credentials;
        match_result(
            process_credentials(request->client_pid),
            success(value)
            {
                credentials = value;
            },
            error(code, message)
            {
                return IOReply__new(IO_UNKNOWN_PID, 0);
            }
        );
        IOStatus access = Memfs__search_parents(self, &path, &credentials);
        if (access != IO_OK)
        {
            return IOReply__new(access, 0);
        }
        switch (request->operation)
        {
        case IO_REQUEST_OPEN:
            return Memfs__open(self, request, &path, &credentials);
        case IO_REQUEST_LIST_DIR:
            return Memfs__list(self, &path, request->offset, &credentials);
        case IO_REQUEST_FILE_INFO:
            return Memfs__file_info(self, &path);
        case IO_REQUEST_REMOVE:
            return Memfs__remove(self, &path, request->flags, &credentials);
        default:
            if (Memfs__directory(self, &path))
            {
                return IOReply__new(IO_INVALID_PATH, 0);
            }
            match_optional(
                Memfs__find(self, &path),
                some(node)
                {
                    return IOReply__new(IO_INVALID_PATH, 0);
                },
                none_do_nothing()
            );
            return IOReply__new(Memfs__create(self, &path, true, &credentials), 0);
        }
    }
    if (request->operation != IO_REQUEST_READ && request->operation != IO_REQUEST_WRITE &&
        request->operation != IO_REQUEST_SEEK && request->operation != IO_REQUEST_CLOSE)
    {
        return IOReply__new(IO_OPERATION_NOT_SUPPORTED, 0);
    }
    match_find_result_ptr(
        MemfsHandles__find(&self->handles, MemfsHandle__matches, request),
        some(handle)
        {
            if (request->operation == IO_REQUEST_CLOSE)
            {
                match_result(
                    MemfsHandles__remove(&self->handles, *handle),
                    success(code)
                    {
                        return IOReply__new(IO_OK, 0);
                    },
                    error(code, message)
                    {
                        return IOReply__new(IO_UNKNOWN_HANDLE, 0);
                    }
                );
            }
            match_find_result_ptr(
                MemfsNodes__find(&self->nodes, MemfsNode__id_matches, handle),
                some(node)
                {
                    if (request->operation == IO_REQUEST_SEEK)
                    {
                        return MemfsNode__seek(node, handle, request);
                    }
                    return MemfsNode__transfer(node, handle, request);
                },
                none()
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

Optional(IOReply) Memfs__serve(IOServiceContext context, QueueConnectionID session, uint64_t pid, IORequestPtr request)
{
    MemfsPtr self = context;
    if (pid == 0)
    {
        return Some(IOReply, IOReply__new(IO_PERMISSION_DENIED, 0));
    }
    return Some(IOReply, Memfs__execute(self, request));
}

bool Memfs__disconnect(IOServiceContext context, QueueConnectionID session, uint64_t pid)
{
    MemfsPtr self = context;
    IORequest request = IORequest__new(IO_REQUEST_RELEASE_CLIENT, 0);
    request.client_session = session;
    request.client_pid = pid;
    return Memfs__release(self, &request).status == IO_OK;
}
