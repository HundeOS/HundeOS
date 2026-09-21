#include "namespace.h"
#include "types/bstring_additional_types.h"
#include "io/path.h"
#include "sys/print.h"
#include "sys/service.h"

FixedArrayBodyDefine(FilesystemMounts);
FixedArrayDefine(BString, 13, MountChildren, bstring_equals);
FixedArrayBodyDefine(MountChildren);
typedef MountChildren * MountChildrenPtr;

bool FilesystemMount__equals(void * first, void * second)
{
    return BString__equals(&((FilesystemMountPtr)first)->config.mountpoint,
                           &((FilesystemMountPtr)second)->config.mountpoint);
}

FilesystemNamespace FilesystemNamespace__new(void)
{
    return (FilesystemNamespace){.mounts = FilesystemMounts__new()};
}

static bool FilesystemNamespace__within(ConstBStringPtr path, ConstBStringPtr mountpoint)
{
    StringPos length = BString__length(mountpoint);
    return BString__starts_with(path, mountpoint) &&
           (BString__equals(mountpoint, &bstr("/")) || BString__length(path) == length ||
            BString__char_at_or(path, length, 0) == '/');
}

static bool FilesystemNamespace__valid_path(ConstBStringPtr path)
{
    if (BString__length(path) >= IO_PROTOCOL_DATA_SIZE || !BString__starts_with(path, &bstr("/")))
    {
        return false;
    }
    BString remaining = *path;
    while (!BString__is_empty(&remaining))
    {
        BStringSplit split = BString__split_first_token(&remaining, &bstr("/"));
        match_optional(
            split.match,
            some(segment)
            {
                if (BString__equals(&segment, &bstr("..")) || BString__equals(&segment, &bstr(".")))
                {
                    return false;
                }
            },
            none_do_nothing()
        );
        remaining = split.rest;
    }
    return true;
}

bool FilesystemMount__matches_mountpoint(FilesystemMountPtr self, void * context)
{
    return BString__equals(&self->config.mountpoint, context);
}

static FilesystemMount FilesystemMount__new(MountConfigPtr config, IOHandle first_handle)
{
    return (FilesystemMount){.config = *config, .first_handle = first_handle};
}

Result(IOStatus) FilesystemNamespace__add(FilesystemNamespacePtr self, MountConfig config)
{
    if (!FilesystemNamespace__valid_path(&config.mountpoint) ||
        (!BString__equals(&config.mountpoint, &bstr("/")) &&
         BString__char_at_or(&config.mountpoint, BString__length(&config.mountpoint) - 1, 0) == '/') ||
        (!BString__equals(&config.filesystem_type, &bstr("fat32")) &&
         !BString__equals(&config.filesystem_type, &bstr("ext2")) &&
         !BString__equals(&config.filesystem_type, &bstr("memfs")) &&
         !BString__equals(&config.filesystem_type, &bstr("devfs")) &&
         !BString__equals(&config.filesystem_type, &bstr("procfs")) &&
         !BString__equals(&config.filesystem_type, &bstr("embedfs")) &&
         !BString__equals(&config.filesystem_type, &bstr("sysinfofs"))))
    {
        return Error(IOStatus, bstr("Invalid userspace mount"), IO_INVALID_MOUNT_POINT);
    }
    match_find_result(
        FilesystemMounts__find(&self->mounts, FilesystemMount__matches_mountpoint, &config.mountpoint),
        some(mount)
        {
            return Error(IOStatus, bstr("Duplicate mount"), IO_INVALID_MOUNT_POINT);
        },
        none_do_nothing()
    );
    FilesystemMount mount = FilesystemMount__new(
        &config, IO_FILESYSTEM_HANDLE_BASE + FilesystemMounts__len(&self->mounts) * IO_SESSION_HANDLE_BASE);
    match_result(
        FilesystemMounts__push(&self->mounts, mount),
        success(code)
        {
            return Success(IOStatus, IO_OK);
        },
        error(code, message)
        {
            return Error(IOStatus, message, IO_REGISTER_FAILED);
        }
    );
}

static Optional(FilesystemMountPtr) FilesystemNamespace__route(FilesystemNamespacePtr self, ConstBStringPtr path)
{
    Optional(FilesystemMountPtr) selected = None(FilesystemMountPtr);
    StringPos longest = 0;
    // Mount selection is a longest-component-prefix reduction, not a first match.
    for_each_fa_ptr(
        FilesystemMounts,
        (&self->mounts),
        index,
        mount,
        {
            StringPos length = BString__length(&mount.config.mountpoint);
            if (length > longest && FilesystemNamespace__within(path, &mount.config.mountpoint))
            {
                match_optional(
                    FilesystemMounts__ptr_at(&self->mounts, index),
                    some(value)
                    {
                        selected = Some(FilesystemMountPtr, value);
                    },
                    none_do_nothing()
                );
                longest = length;
            }
        }
    );
    return selected;
}

static bool FilesystemMount__matches_handle(FilesystemMountPtr self, void * context)
{
    IOHandle handle = *(IOHandlePtr)context;
    return handle >= self->first_handle && handle < self->first_handle + IO_SESSION_HANDLE_BASE;
}

static BString FilesystemMount__relative_path(FilesystemMountPtr self, ConstBStringPtr path)
{
    BString relative = BString__equals(&self->config.mountpoint, &bstr("/"))
                           ? *path
                           : BString__slice(path, BString__length(&self->config.mountpoint), BString__length(path));
    return BString__is_empty(&relative) ? bstr("/") : relative;
}

static Optional(IOReply) FilesystemMount__execute(FilesystemMountPtr self, IORequestPtr request)
{
    if (!self->connected)
    {
        match_optional(
            ServiceDirectory__find_endpoint(ServiceDirectory__shared(), &self->config.mountpoint),
            some(endpoint)
            {
            },
            none()
            {
                return Some(IOReply, IOReply__new(IO_NO_DEVICE_FOUND, 0));
            }
        );
        match_result(
            Channel__open(ServiceDirectory__shared(), &self->config.mountpoint),
            success(channel)
            {
                self->mediator = IOMediator__new_filesystem(channel, self->first_handle);
                self->connected = true;
            },
            error(code, message)
            {
                return Some(IOReply, IOReply__new(IO_NO_DEVICE_FOUND, 0));
            }
        );
    }
    match_optional(
        IOMediator__execute(&self->mediator, request->client_session, request->client_pid, request),
        some(reply)
        {
            return Some(IOReply, reply);
        },
        none()
        {
            return Some(IOReply, IOReply__new(IO_WAIT_FOR_DATA, 0));
        }
    );
}

static void MountChildren__add_path(MountChildrenPtr self, ConstBStringPtr parent, ConstBStringPtr mountpoint)
{
    if (!BString__starts_with(mountpoint, parent))
    {
        return;
    }
    BString relative = BString__slice(mountpoint, BString__length(parent), BString__length(mountpoint));
    BStringSplit split = BString__split_first_token(&relative, &bstr("/"));
    match_optional(
        split.match,
        some(name)
        {
            match_optional(MountChildren__find_item(self, name),
                some(found)
                {
                    return;
                },
                none_do_nothing()
            );
            // At most eight configured mounts and five bootstrap paths contribute.
            match_result(
                MountChildren__push(self, name),
                success_do_nothing(),
                error(code, message)
                {
                    IO__printf(&bstr("Mount overlay capacity exceeded: %B\n"), &message);
                }
            );
        },
        none_do_nothing()
    );
}

static MountChildren FilesystemNamespace__children(FilesystemNamespacePtr self, ConstBStringPtr path)
{
    BString parent = BString__equals(path, &bstr("/")) ? *path : BString__append(path, &bstr("/"));
    MountChildren children = MountChildren__new();
    for_each_fa_ptr(
        FilesystemMounts,
        (&self->mounts),
        index,
        mount,
        {
            MountChildren__add_path(&children, &parent, &mount.config.mountpoint);
        }
    );
    MountChildren__add_path(&children, &parent, &bstr("/sbin"));
    MountChildren__add_path(&children, &parent, &bstr("/boot/config"));
    MountChildren__add_path(&children, &parent, &bstr("/boot/sysinfo"));
    return children;
}

static bool FilesystemNamespace__release(FilesystemNamespacePtr self, QueueConnectionID session, uint64_t pid)
{
    bool complete = true;
    for (ArrayIndex index = 0; index < FilesystemMounts__len(&self->mounts); index++)
    {
        match_optional(
            FilesystemMounts__ptr_at(&self->mounts, index),
            some(mount)
            {
                if (mount->connected)
                {
                    complete = IOMediator__disconnect(&mount->mediator, session, pid) && complete;
                }
            },
            none_do_nothing()
        );
    }
    return complete;
}

Optional(IOReply) FilesystemNamespace__execute(IOServiceContext context, QueueConnectionID session, uint64_t pid,
                                               IORequestPtr request)
{
    FilesystemNamespacePtr self = context;
    if (request->operation == IO_REQUEST_MOUNT)
    {
        if (pid == 0 || !service_ok(service_authorize_mount_client(pid)) || request->count >= IO_PROTOCOL_DATA_SIZE)
        {
            return Some(IOReply, IOReply__new(IO_PERMISSION_DENIED, 0));
        }
        BString line = IORequest__path(request);
        match_result(
            MountConfig__parse(&line),
            success(config)
            {
                match_result(
                    FilesystemNamespace__add(self, config),
                    success(status)
                    {
                        return Some(IOReply, IOReply__new(IO_OK, 0));
                    },
                    error(code, message)
                    {
                        return Some(IOReply, IOReply__new(code, 0));
                    }
                );
            },
            error(code, message)
            {
                return Some(IOReply, IOReply__new(IO_INVALID_MOUNT_POINT, 0));
            }
        );
    }
    uint64_t io_pid =
        unwrap_optional(ServiceDirectory__find_service_pid(ServiceDirectory__shared(), &bstr(IO_SYSTEM_SERVICE)), 0);
    if (pid == 0 || pid != io_pid || (self->mediator_session != 0 && self->mediator_session != session) ||
        request->client_pid == 0 || request->client_session == 0)
    {
        return Some(IOReply, IOReply__new(IO_PERMISSION_DENIED, 0));
    }
    self->mediator_session = session;
    if (request->operation == IO_REQUEST_RELEASE_CLIENT)
    {
        return FilesystemNamespace__release(self, request->client_session, request->client_pid)
                   ? Some(IOReply, IOReply__new(IO_OK, 0))
                   : Some(IOReply, IOReply__new(IO_WAIT_FOR_DATA, 0));
    }
    if (request->operation == IO_REQUEST_OPEN || request->operation == IO_REQUEST_LIST_DIR ||
        request->operation == IO_REQUEST_MKDIR || request->operation == IO_REQUEST_REMOVE ||
        request->operation == IO_REQUEST_LINK)
    {
        BString path = IORequest__path(request);
        if (!FilesystemNamespace__valid_path(&path))
        {
            return Some(IOReply, IOReply__new(IO_INVALID_PATH, 0));
        }
        BString target_path = IORequest__target_path(request);
        if (request->operation == IO_REQUEST_LINK && !FilesystemNamespace__valid_path(&target_path))
        {
            return Some(IOReply, IOReply__new(IO_INVALID_PATH, 0));
        }
        Optional(FilesystemMountPtr) route = FilesystemNamespace__route(self, &path);
        uint64_t child_count = 0;
        if (request->operation == IO_REQUEST_LIST_DIR)
        {
            MountChildren children = FilesystemNamespace__children(self, &path);
            child_count = MountChildren__len(&children);
            match_optional(
                MountChildren__at(&children, request->offset),
                some(name)
                {
                    IOReply reply = IOReply__new(IO_OK, 1);
                    FileMode mode = BString__equals(&path, &bstr("/boot")) && BString__equals(&name, &bstr("sysinfo"))
                                        ? IO_MODE_TYPE_FILE | 0444
                                        : IO_MODE_TYPE_DIR | 0755;
                    reply.entry =
                        DirectoryEntry__new(IO_DIRENT_ATTR_NONE, PROCESS_USER_ROOT, PROCESS_GROUP_ROOT, mode, &name);
                    return Some(IOReply, reply);
                },
                none_do_nothing()
            );
        }
        match_optional(
            route,
            some(mount)
            {
                BString relative = FilesystemMount__relative_path(mount, &path);
                IORequest forwarded = *request;
                forwarded.count = BString__copy_to(&relative, (char *)forwarded.data, sizeof(forwarded.data));
                if (request->operation == IO_REQUEST_LINK)
                {
                    match_optional(
                        FilesystemNamespace__route(self, &target_path),
                        some(target_mount)
                        {
                            if (mount != target_mount)
                            {
                                return Some(IOReply, IOReply__new(IO_OPERATION_NOT_SUPPORTED, 0));
                            }
                            BString target_relative = FilesystemMount__relative_path(mount, &target_path);
                            IORequest__set_target_path(&forwarded, &target_relative);
                        },
                        none()
                        {
                            return Some(IOReply, IOReply__new(IO_INVALID_PATH, 0));
                        }
                    );
                }
                if (request->operation == IO_REQUEST_LIST_DIR)
                {
                    forwarded.offset -= child_count;
                    match_optional(
                        FilesystemMount__execute(mount, &forwarded),
                        some(reply)
                        {
                            if (reply.status == IO_INVALID_PATH && child_count > 0)
                            {
                                return Some(IOReply, IOReply__new(IO_OK, 0));
                            }
                            return Some(IOReply, reply);
                        },
                        none()
                        {
                            return None(IOReply);
                        }
                    );
                }
                return FilesystemMount__execute(mount, &forwarded);
            },
            none()
            {
                return Some(
                    IOReply,
                    IOReply__new(request->operation == IO_REQUEST_LIST_DIR && child_count > 0 ? IO_OK : IO_INVALID_PATH,
                                 0));
            }
        );
    }
    match_find_result_ptr(
        FilesystemMounts__find(&self->mounts, FilesystemMount__matches_handle, &request->handle),
        some(mount)
        {
            return FilesystemMount__execute(mount, request);
        },
        none()
        {
            return Some(IOReply, IOReply__new(IO_UNKNOWN_HANDLE, 0));
        }
    );
}

bool FilesystemNamespace__disconnect(IOServiceContext context, QueueConnectionID session, uint64_t pid)
{
    FilesystemNamespacePtr self = context;
    if (self->mediator_session != session)
    {
        return true;
    }
    for (ArrayIndex index = 0; index < FilesystemMounts__len(&self->mounts); index++)
    {
        match_optional(
            FilesystemMounts__ptr_at(&self->mounts, index),
            some(mount)
            {
                if (mount->connected && !mount->mediator.failed)
                {
                    Channel__close(&mount->mediator.backend);
                    mount->mediator.failed = true;
                }
            },
            none_do_nothing()
        );
    }
    self->mediator_session = 0;
    return true;
}
