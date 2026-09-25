#include "embedfs.h"
#include "sys/process.h"
#include <string.h>

FixedArrayBodyDefine(EmbedfsHandles);

bool EmbedfsHandle__equals(void * first, void * second)
{
    EmbedfsHandlePtr left = first;
    EmbedfsHandlePtr right = second;
    return left->id == right->id;
}

static EmbedfsHandle EmbedfsHandle__new(IOHandle id, IORequestPtr request, EmbeddedFsFilePtr file)
{
    return (EmbedfsHandle){
        .id = id, .session = request->client_session, .pid = request->client_pid, .file = *file, .offset = 0};
}

Embedfs Embedfs__new(EmbeddedFsPtr archive)
{
    return (Embedfs){.archive = *archive,
                     .handles = EmbedfsHandles__new(),
                     .children = BStringArray__new(),
                     .next_handle = IO_SESSION_HANDLE_BASE,
                     .namespace_session = 0};
}

static bool EmbedfsHandle__matches(EmbedfsHandlePtr self, void * context)
{
    IORequestPtr request = context;
    return self->id == request->handle && self->session == request->client_session && self->pid == request->client_pid;
}

static bool EmbedfsHandle__belongs_to(EmbedfsHandlePtr self, void * context)
{
    IORequestPtr request = context;
    return self->session == request->client_session && self->pid == request->client_pid;
}

static bool Embedfs__restricted(ConstBStringPtr path)
{
    return BString__equals(path, &bstr("drivers")) || BString__starts_with(path, &bstr("drivers/")) ||
           BString__equals(path, &bstr("services")) || BString__starts_with(path, &bstr("services/"));
}

static Optional(BString) Embedfs__relative_path(IORequestPtr request)
{
    BString path = IORequest__path(request);
    if (BString__contains_char(&path, '\0'))
    {
        return None(BString);
    }
    if (BString__starts_with(&path, &bstr("/")))
    {
        path = BString__slice(&path, 1, BString__length(&path));
    }
    BString remaining = path;
    while (!BString__is_empty(&remaining))
    {
        BStringSplit split = BString__split_first_token(&remaining, &bstr("/"));
        BString segment = unwrap_optional(split.match, remaining);
        if (BString__is_empty(&segment) || BString__equals(&segment, &bstr(".")) ||
            BString__equals(&segment, &bstr("..")))
        {
            return None(BString);
        }
        remaining = split.rest;
    }
    return Some(BString, path);
}

static IOReply Embedfs__list(EmbedfsPtr self, ConstBStringPtr path, uint64_t requested)
{
    self->children = BStringArray__new();
    IOReply reply = IOReply__new(IO_OK, 0);
    BString prefix = BString__is_empty(path) ? empty_bstr() : BString__append(path, &bstr("/"));
    for (uint64_t index = 0; index < EmbeddedFs__entry_count(&self->archive); index++)
    {
        match_optional(
            EmbeddedFs__at(&self->archive, index),
            some(file)
            {
                if (!BString__is_empty(&prefix) && !BString__starts_with(&file.name, &prefix))
                {
                    continue;
                }
                BString relative = BString__slice(&file.name, BString__length(&prefix), BString__length(&file.name));
                BStringSplit split = BString__split_first_token(&relative, &bstr("/"));
                BString child = unwrap_optional(split.match, relative);
                match_optional(
                    BStringArray__find_item(&self->children, child),
                    some(found)
                    {
                        continue;
                    },
                    none_do_nothing()
                );
                match_result(
                    BStringArray__push(&self->children, child),
                    success_do_nothing(),
                    error(code, message)
                    {
                        return IOReply__new(IO_UNKNOWN_BUFFER, 0);
                    }
                );
                uint64_t child_index = BStringArray__len(&self->children) - 1;
                if (child_index >= requested)
                {
                    BString full = BString__append(&prefix, &child);
                    bool restricted = Embedfs__restricted(&full);
                    bool directory = !BString__is_empty(&split.rest);
                    reply.directory.entries[reply.directory.count++] = DirectoryEntry__new(
                        0, restricted ? PROCESS_USER_HUND : PROCESS_USER_ROOT,
                        restricted ? PROCESS_GROUP_HUND : PROCESS_GROUP_ROOT,
                        (directory ? IO_MODE_TYPE_DIR : IO_MODE_TYPE_FILE) | (restricted ? 0550 : 0555), &child);
                    if (reply.directory.count == IO_DIRECTORY_BATCH_CAPACITY)
                    {
                        return reply;
                    }
                }
            },
            none()
            {
                return IOReply__new(IO_INVALID_PATH, 0);
            }
        );
    }
    return BString__is_empty(path) || BStringArray__len(&self->children) > 0 ? reply : IOReply__new(IO_INVALID_PATH, 0);
}

static IOReply Embedfs__file_info(EmbedfsPtr self, ConstBStringPtr path)
{
    IOReply reply = IOReply__new(IO_OK, 0);
    bool restricted = Embedfs__restricted(path);
    match_optional(
        EmbeddedFs__find(&self->archive, path),
        some(file)
        {
            reply.file_info = IOFileInfo__new(
                IO_MODE_TYPE_FILE | (restricted ? 0550 : 0555), restricted ? PROCESS_USER_HUND : PROCESS_USER_ROOT,
                restricted ? PROCESS_GROUP_HUND : PROCESS_GROUP_ROOT, file.end - file.start);
            return reply;
        },
        none_do_nothing()
    );
    BString prefix = BString__is_empty(path) ? empty_bstr() : BString__append(path, &bstr("/"));
    for (uint64_t index = 0; index < EmbeddedFs__entry_count(&self->archive); index++)
    {
        match_optional(
            EmbeddedFs__at(&self->archive, index),
            some(file)
            {
                if (BString__is_empty(path) || BString__starts_with(&file.name, &prefix))
                {
                    reply.file_info = IOFileInfo__new(IO_MODE_TYPE_DIR | (restricted ? 0550 : 0555),
                                                      restricted ? PROCESS_USER_HUND : PROCESS_USER_ROOT,
                                                      restricted ? PROCESS_GROUP_HUND : PROCESS_GROUP_ROOT, 0);
                    return reply;
                }
            },
            none()
            {
                return IOReply__new(IO_INVALID_PATH, 0);
            }
        );
    }
    return IOReply__new(IO_INVALID_PATH, 0);
}

static IOReply Embedfs__open(EmbedfsPtr self, IORequestPtr request, ConstBStringPtr path)
{
    if ((request->flags & IO_OPEN_READ) == 0 || (request->flags & ~(IO_OPEN_READ | IO_OPEN_EXECUTE)) != 0)
    {
        return IOReply__new(IO_PERMISSION_DENIED, 0);
    }
    if (self->next_handle == INT64_MAX)
    {
        return IOReply__new(IO_UNKNOWN_HANDLE, 0);
    }
    match_optional(
        EmbeddedFs__find(&self->archive, path),
        some(file)
        {
            EmbedfsHandle handle = EmbedfsHandle__new(self->next_handle++, request, &file);
            match_result(
                EmbedfsHandles__push(&self->handles, handle),
                success(code)
                {
                    return IOReply__new(IO_OK, handle.id);
                },
                error(code, message)
                {
                    return IOReply__new(IO_UNKNOWN_HANDLE, 0);
                }
            );
        },
        none()
        {
            return IOReply__new(IO_INVALID_PATH, 0);
        }
    );
}

IOReply Embedfs__execute(EmbedfsPtr self, IORequestPtr request)
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
        while (true)
        {
            match_find_result(
                EmbedfsHandles__find(&self->handles, EmbedfsHandle__belongs_to, request),
                some(handle)
                {
                    match_result(
                        EmbedfsHandles__remove(&self->handles, handle),
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
    if (request->operation == IO_REQUEST_OPEN || request->operation == IO_REQUEST_LIST_DIR ||
        request->operation == IO_REQUEST_FILE_INFO)
    {
        BString path;
        match_optional(
            Embedfs__relative_path(request),
            some(value)
            {
                path = value;
            },
            none()
            {
                return IOReply__new(IO_INVALID_PATH, 0);
            }
        );
        match_result(
            process_credentials(request->client_pid),
            success(credentials)
            {
                if (Embedfs__restricted(&path) && credentials.user_id != PROCESS_USER_ROOT &&
                    credentials.group_id != PROCESS_GROUP_HUND)
                {
                    return IOReply__new(IO_PERMISSION_DENIED, 0);
                }
            },
            error(code, message)
            {
                return IOReply__new(IO_UNKNOWN_PID, 0);
            }
        );
        if (request->operation == IO_REQUEST_OPEN)
        {
            return Embedfs__open(self, request, &path);
        }
        return request->operation == IO_REQUEST_LIST_DIR ? Embedfs__list(self, &path, request->offset)
                                                         : Embedfs__file_info(self, &path);
    }
    if (request->operation != IO_REQUEST_READ && request->operation != IO_REQUEST_CLOSE)
    {
        return IOReply__new(IO_PERMISSION_DENIED, 0);
    }
    match_find_result_ptr(
        EmbedfsHandles__find(&self->handles, EmbedfsHandle__matches, request),
        some(handle)
        {
            if (request->operation == IO_REQUEST_CLOSE)
            {
                match_result(
                    EmbedfsHandles__remove(&self->handles, *handle),
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
            size_t remaining = handle->file.end - handle->file.start - handle->offset;
            size_t size = request->count < remaining ? request->count : remaining;
            IOReply reply = IOReply__new(IO_OK, size);
            memcpy(IORequest__read_target(request, &reply), (const void *)(handle->file.start + handle->offset), size);
            handle->offset += size;
            return reply;
        },
        none()
        {
            return IOReply__new(IO_UNKNOWN_HANDLE, 0);
        }
    );
}

Optional(IOReply)
    Embedfs__serve(IOServiceContext context, QueueConnectionID session, uint64_t pid, IORequestPtr request)
{
    EmbedfsPtr self = context;
    uint64_t namespace_pid = unwrap_optional(
        ServiceDirectory__find_service_pid(ServiceDirectory__shared(), &bstr(IO_FILESYSTEM_SERVICE)), 0);
    if (pid == 0 || pid != namespace_pid || (self->namespace_session != 0 && self->namespace_session != session))
    {
        return Some(IOReply, IOReply__new(IO_PERMISSION_DENIED, 0));
    }
    self->namespace_session = session;
    return Some(IOReply, Embedfs__execute(self, request));
}

bool Embedfs__disconnect(IOServiceContext context, QueueConnectionID session, uint64_t pid)
{
    EmbedfsPtr self = context;
    if (self->namespace_session == session)
    {
        self->handles = EmbedfsHandles__new();
        self->namespace_session = 0;
    }
    return true;
}
