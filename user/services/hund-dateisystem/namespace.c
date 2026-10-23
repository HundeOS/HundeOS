#include "namespace.h"
#include "io/path.h"
#include "sys/service.h"

FixedArrayBodyDefine(FilesystemMounts);
FixedArrayDefine(BString, 16, FilesystemNamespaceChildren, bstring_equals);
FixedArrayStackBodyDefine(FilesystemNamespaceChildren);
typedef FilesystemNamespaceChildren * FilesystemNamespaceChildrenPtr;

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

static void FilesystemMount__bind_config(FilesystemMountPtr self)
{
    BString device_path = BString__from_chars((const char *)self->device_path_data, self->device_path_length);
    BString filesystem_type =
        BString__from_chars((const char *)self->filesystem_type_data, self->filesystem_type_length);
    BString mountpoint = BString__from_chars((const char *)self->mountpoint_data, self->mountpoint_length);
    self->config = MountConfig__new(&device_path, &filesystem_type, &mountpoint);
}

static FilesystemMount FilesystemMount__new(MountConfigPtr config)
{
    FilesystemMount self = {0};
    self.device_path_length =
        BString__copy_to(&config->device_path, (char *)self.device_path_data, sizeof(self.device_path_data));
    self.filesystem_type_length = BString__copy_to(&config->filesystem_type, (char *)self.filesystem_type_data,
                                                   sizeof(self.filesystem_type_data));
    self.mountpoint_length =
        BString__copy_to(&config->mountpoint, (char *)self.mountpoint_data, sizeof(self.mountpoint_data));
    FilesystemMount__bind_config(&self);
    return self;
}

Result(IOStatus) FilesystemNamespace__add(FilesystemNamespacePtr self, MountConfig config)
{
    if (!FilesystemNamespace__valid_path(&config.mountpoint) ||
        (!BString__equals(&config.mountpoint, &bstr("/")) &&
         BString__char_at_or(&config.mountpoint, BString__length(&config.mountpoint) - 1, 0) == '/') ||
        (!BString__equals(&config.filesystem_type, &bstr("fat32")) &&
         !BString__equals(&config.filesystem_type, &bstr("ext2")) &&
         !BString__equals(&config.filesystem_type, &bstr("ext2.wuff")) &&
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
    FilesystemMount mount = FilesystemMount__new(&config);
    match_result(
        FilesystemMounts__push(&self->mounts, mount),
        success(index)
        {
            match_optional(
                FilesystemMounts__ptr_at(&self->mounts, index),
                some(stored)
                {
                    FilesystemMount__bind_config(stored);
                },
                none()
                {
                    return Error(IOStatus, bstr("Stored mount is unavailable"), IO_REGISTER_FAILED);
                }
            );
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

static BString FilesystemMount__relative_path(FilesystemMountPtr self, ConstBStringPtr path)
{
    BString relative = BString__equals(&self->config.mountpoint, &bstr("/"))
                           ? *path
                           : BString__slice(path, BString__length(&self->config.mountpoint), BString__length(path));
    return BString__is_empty(&relative) ? bstr("/") : relative;
}

static Optional(BString) FilesystemNamespace__child_name(ConstBStringPtr path, ConstBStringPtr mountpoint)
{
    if (BString__equals(path, mountpoint) || !BString__starts_with(mountpoint, path))
    {
        return None(BString);
    }

    StringPos path_length = BString__length(path);
    bool root = BString__equals(path, &bstr("/"));
    bool trailing_slash = BString__char_at_or(path, path_length - 1, 0) == '/';
    StringPos child_start = path_length + 1;
    if (root)
    {
        child_start = 1;
    }
    else if (trailing_slash)
    {
        child_start = path_length;
    }
    if ((!root && !trailing_slash && BString__char_at_or(mountpoint, path_length, 0) != '/') ||
        child_start >= BString__length(mountpoint))
    {
        return None(BString);
    }

    BString descendants = BString__slice(mountpoint, child_start, BString__length(mountpoint));
    BStringSplit split = BString__split_first_token(&descendants, &bstr("/"));
    return split.match;
}

static FilesystemNamespaceChildren FilesystemNamespace__children(FilesystemNamespacePtr self, ConstBStringPtr path)
{
    FilesystemNamespaceChildren children = FilesystemNamespaceChildren__new();
    for_each_fa_ptr(
        FilesystemMounts,
        (&self->mounts),
        index,
        mount,
        {
            match_optional(
                FilesystemNamespace__child_name(path, &mount.config.mountpoint),
                some(child)
                {
                    match_optional(
                        FilesystemNamespaceChildren__find_item(&children, child),
                        some_do_nothing(),
                        none()
                        {
                            match_result(
                                FilesystemNamespaceChildren__push(&children, child),
                                success_do_nothing(),
                                error(code, message)
                                {
                                    return children;
                                }
                            );
                        }
                    );
                },
                none_do_nothing()
            );
        }
    );
    return children;
}

static IOReply FilesystemNamespaceChildren__list(FilesystemNamespaceChildrenPtr self, uint64_t offset)
{
    IOReply reply = IOReply__new(IO_OK, 0);
    for (uint64_t index = offset;
         index < FilesystemNamespaceChildren__len(self) && reply.directory.count < IO_DIRECTORY_BATCH_CAPACITY; index++)
    {
        match_optional(
            FilesystemNamespaceChildren__ptr_at(self, index),
            some(child)
            {
                reply.directory.entries[reply.directory.count++] = DirectoryEntry__new(
                    IO_DIRENT_ATTR_NONE, PROCESS_USER_ROOT, PROCESS_GROUP_ROOT, IO_MODE_TYPE_DIR | 0755, child);
            },
            none_do_nothing()
        );
    }
    return reply;
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

    if (pid == 0)
    {
        return Some(IOReply, IOReply__new(IO_PERMISSION_DENIED, 0));
    }
    if (request->operation == IO_REQUEST_OPEN || request->operation == IO_REQUEST_LIST_DIR ||
        request->operation == IO_REQUEST_FILE_INFO || request->operation == IO_REQUEST_MKDIR ||
        request->operation == IO_REQUEST_REMOVE || request->operation == IO_REQUEST_LINK)
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
        uint64_t child_count = 0;
        if (request->operation == IO_REQUEST_LIST_DIR)
        {
            FilesystemNamespaceChildren children = FilesystemNamespace__children(self, &path);
            child_count = FilesystemNamespaceChildren__len(&children);
            if (request->offset < child_count)
            {
                return Some(IOReply, FilesystemNamespaceChildren__list(&children, request->offset));
            }
        }
        Optional(FilesystemMountPtr) route = FilesystemNamespace__route(self, &path);
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
                BString rewritten = IORequest__path(&forwarded);
                BString rewritten_target = IORequest__target_path(&forwarded);
                IOReply reply = IOReply__redirect(&mount->config.mountpoint, &rewritten, &rewritten_target, true);
                reply.value =
                    request->operation == IO_REQUEST_LIST_DIR ? forwarded.offset - child_count : forwarded.offset;
                return Some(IOReply, reply);
            },
            none()
            {
                return Some(IOReply, IOReply__new(IO_INVALID_PATH, 0));
            }
        );
    }
    return Some(IOReply, IOReply__new(IO_UNKNOWN_HANDLE, 0));
}

bool FilesystemNamespace__disconnect(IOServiceContext context, QueueConnectionID session, uint64_t pid)
{
    return true;
}
