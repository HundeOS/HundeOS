#include "io.h"

#include <stddef.h>

#include "syscall.h"
#include "io/client.h"

#include "io/path.h"

IOHandle open(ConstBStringPtr path, Flags flags)
{
    if (!IOPath__is_bootstrap(path))
    {
        IOHandle handle = IOClient__open(path, flags);
        if (handle != IO_PATH_UNMANAGED)
        {
            return handle;
        }
    }
    IOPathRequest request = IOPathRequest__new(path, flags);
    return syscall(IO_CALL, IO_OPEN, any(&request), 0, 0, 0);
}

ByteCount read(IOHandle handle, char * buf, size_t size)
{
    if (handle >= IO_SESSION_HANDLE_BASE)
    {
        return IOClient__read(handle, (ChannelDataPtr)buf, size);
    }
    return syscall(IO_CALL, IO_READ, handle, any(buf), size, 0);
}

ByteCount write(IOHandle handle, char * buf, size_t size)
{
    if (handle >= IO_SESSION_HANDLE_BASE)
    {
        return IOClient__write(handle, (ConstChannelDataPtr)buf, size);
    }
    return syscall(IO_CALL, IO_WRITE, handle, any(buf), size, 0);
}

IOHandle IO__open(ConstBStringPtr path, Flags flags)
{
    return open(path, flags);
}

IOHandle accept(IOHandle handle)
{
    if (handle >= IO_SESSION_HANDLE_BASE)
    {
        return IOClient__accept(handle);
    }
    return syscall(IO_CALL, IO_ACCEPT, handle, 0, 0, 0);
}

IOHandle IO__accept(IOHandle handle)
{
    return accept(handle);
}

BString IO__path_from_arg(ConstBStringPtr arg)
{
    BString slash = bstr("/");
    if (BString__is_empty(arg))
    {
        return slash;
    }

    if (BString__starts_with(arg, &bstr("/")))
    {
        return *arg;
    }

    return BString__append(&bstr("/"), arg);
}

BString IO__path_join(ConstBStringPtr base, ConstBStringPtr child)
{
    if (BString__is_empty(base) || BString__equals(base, &bstr("/")))
    {
        return BString__append(&bstr("/"), child);
    }

    BString with_slash = BString__append(base, &bstr("/"));
    return BString__append(&with_slash, child);
}

static ByteCount IO__read_bstring_with_limit(IOHandle handle, BStringPtr buffer, StringPos max_bytes)
{
    BString__clear(buffer);
    if (max_bytes <= 0)
    {
        return 0;
    }

    StringPos buffer_max = BSTRING_MAX_SIZE - 1;
    StringPos read_max = max_bytes < buffer_max ? max_bytes : buffer_max;

    ByteCount bytes = read(handle, (char *)BString__cstr(buffer), (size_t)read_max);
    if (bytes <= 0)
    {
        return bytes;
    }

    if (bytes > read_max)
    {
        bytes = read_max;
    }

    *buffer = BString__from_chars(BString__cstr(buffer), bytes);
    return bytes;
}

ByteCount IO__read(IOHandle handle, BStringPtr buffer)
{
    return IO__read_bstring_with_limit(handle, buffer, BSTRING_MAX_SIZE - 1);
}

Optional(DynString) IO__read_dyn_string(IOHandle handle, StringPos max_bytes)
{
    DynString text;
    match_result(
        DynString__init(&text, max_bytes),
        success_do_nothing(),
        error(code, message) { return None(DynString); }
    );

    ByteCount total_read = 0;
    while (total_read < max_bytes)
    {
        BString chunk = empty_bstr();
        ByteCount remaining_capacity = max_bytes - total_read;
        ByteCount bytes = IO__read_bstring_with_limit(handle, &chunk, remaining_capacity);
        if (bytes <= 0)
        {
            break;
        }

        match_result(
            DynString__append_bstring(&text, &chunk),
            success_do_nothing(),
            error(code, message)
            {
                DynString__dispose(&text);
                return None(DynString);
            }
        );

        total_read += bytes;
        if (total_read >= max_bytes)
        {
            break;
        }

        if (bytes < BSTRING_MAX_SIZE - 1)
        {
            break;
        }
    }

    return Some(DynString, text);
}

ByteCount IO__write(IOHandle handle, ConstBStringPtr text)
{
    return write(handle, (char *)BString__cstr(text), (size_t)BString__length(text));
}

IOStatus close(IOHandle handle)
{
    if (handle >= IO_SESSION_HANDLE_BASE)
    {
        return IOClient__close(handle);
    }
    IOStatus result = (IOStatus)syscall(IO_CALL, IO_CLOSE, handle, 0, 0, 0);
    if (result > 0)
    {
        return IO_OK;
    }

    return result;
}

static bool IO__directory_contains(DirectoryEntryPtr entries, size_t count, ConstBStringPtr name)
{
    // The public API supplies an untyped output buffer, not a collection.
    for (size_t index = 0; index < count; index++)
    {
        BString existing = BString__from_chars(entries[index].name, entries[index].name_len);
        if (BString__equals(&existing, name))
        {
            return true;
        }
    }
    return false;
}

DirectoryEntryCount list_dir(ConstBStringPtr path, DirectoryEntryPtr entries, size_t max_entries)
{
    if (!IOPath__is_bootstrap(path) && max_entries > 0)
    {
        IOReply reply = IOClient__path_request(IO_REQUEST_LIST_DIR, path, 0, 0);
        if (reply.status == IO_OK)
        {
            size_t count = 0;
            uint64_t offset = 0;
            while (reply.value == 1 && count < max_entries)
            {
                if (reply.entry.name_len >= IO_DIRENT_NAME_MAX)
                {
                    return IO_UNKNOWN_BUFFER;
                }
                BString name = BString__from_chars(reply.entry.name, reply.entry.name_len);
                // Namespace overlays arrive first and take precedence over disk entries.
                if (!IO__directory_contains(entries, count, &name))
                {
                    entries[count++] = reply.entry;
                }
                if (count == max_entries)
                {
                    break;
                }
                reply = IOClient__path_request(IO_REQUEST_LIST_DIR, path, 0, ++offset);
                if (reply.status != IO_OK)
                {
                    return reply.status;
                }
            }
            return reply.value == 0 || reply.value == 1 ? (DirectoryEntryCount)count : IO_UNKNOWN_BUFFER;
        }
        if (reply.status != IO_PATH_UNMANAGED)
        {
            return reply.status;
        }
        IOListDirRequest legacy = IOListDirRequest__new(path, entries, max_entries);
        DirectoryEntryCount count = syscall(IO_CALL, IO_LIST_DIR, any(&legacy), 0, 0, 0);
        if (count < 0 && reply.value == 0)
        {
            return count;
        }
        if (count < 0)
        {
            count = 0;
        }
        for (uint64_t offset = 0; reply.value == 1 && (size_t)count < max_entries; offset++)
        {
            if (reply.entry.name_len >= IO_DIRENT_NAME_MAX)
            {
                return IO_UNKNOWN_BUFFER;
            }
            BString name = BString__from_chars(reply.entry.name, reply.entry.name_len);
            bool found = false;
            // Merge the mount overlay into the caller-owned syscall output buffer.
            for (DirectoryEntryCount index = 0; index < count; index++)
            {
                BString existing = BString__from_chars(entries[index].name, entries[index].name_len);
                if (BString__equals(&existing, &name))
                {
                    entries[index] = reply.entry;
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                entries[count++] = reply.entry;
            }
            reply = IOClient__path_request(IO_REQUEST_LIST_DIR, path, 0, offset + 1);
            if (reply.status != IO_PATH_UNMANAGED)
            {
                return reply.status == IO_OK ? IO_UNKNOWN_BUFFER : reply.status;
            }
        }
        return count;
    }
    IOListDirRequest request = IOListDirRequest__new(path, entries, max_entries);
    return syscall(IO_CALL, IO_LIST_DIR, any(&request), 0, 0, 0);
}

DirectoryEntryCount IO__list_dir(ConstBStringPtr path, DirectoryEntryPtr entries, size_t max_entries)
{
    return list_dir(path, entries, max_entries);
}

const char * io_status_string(IOStatus status)
{
    switch (status)
    {
    case IO_OK:
        return "IO_OK";
    case IO_NO_DEVICE_FOUND:
        return "IO_NO_DEVICE_FOUND";
    case IO_REGISTER_FAILED:
        return "IO_REGISTER_FAILED";
    case IO_NOT_OPEN:
        return "IO_NOT_OPEN";
    case IO_UNKNOWN_HANDLE:
        return "IO_UNKNOWN_HANDLE";
    case IO_WAIT_FOR_DATA:
        return "IO_WAIT_FOR_DATA";
    case IO_OPERATION_NOT_IMPLEMENTED:
        return "IO_OPERATION_NOT_IMPLEMENTED";
    case IO_OPERATION_NOT_SUPPORTED:
        return "IO_OPERATION_NOT_SUPPORTED";
    case IO_INVALID_MOUNT_POINT:
        return "IO_INVALID_MOUNT_POINT";
    case IO_INVALID_PATH:
        return "IO_INVALID_PATH";
    case IO_EOF:
        return "IO_EOF";
    case IO_UNKNOWN_BUFFER:
        return "IO_UNKNOWN_BUFFER";
    case IO_UNKNOWN_PID:
        return "IO_UNKNOWN_PID";
    case IO_PERMISSION_DENIED:
        return "IO_PERMISSION_DENIED";
    case IO_PATH_UNMANAGED:
        return "IO_PATH_UNMANAGED";
    case IO_PANIC:
        return "IO_PANIC";
    }

    return "IO_UNKNOWN_ERROR";
}

IOStatus make_dir(ConstBStringPtr path)
{
    if (!IOPath__is_bootstrap(path))
    {
        IOStatus status = IOClient__path_request(IO_REQUEST_MKDIR, path, 0, 0).status;
        if (status != IO_PATH_UNMANAGED)
        {
            return status;
        }
    }
    IOPathRequest request = IOPathRequest__new(path, 0);
    return syscall(IO_CALL, IO_MKDIR, any(&request), 0, 0, 0);
}

IOStatus IO__make_dir(ConstBStringPtr path)
{
    return make_dir(path);
}

IOStatus remove_file(ConstBStringPtr path)
{
    return remove_file_with_flags(path, 0);
}

IOStatus IO__remove_file(ConstBStringPtr path)
{
    return remove_file(path);
}

IOStatus remove_file_with_flags(ConstBStringPtr path, Flags flags)
{
    if (!IOPath__is_bootstrap(path))
    {
        IOStatus status = IOClient__path_request(IO_REQUEST_REMOVE, path, flags, 0).status;
        if (status != IO_PATH_UNMANAGED)
        {
            return status;
        }
    }
    IOPathRequest request = IOPathRequest__new(path, flags);
    return syscall(IO_CALL, IO_REMOVE, any(&request), 0, 0, 0);
}

IOStatus IO__remove_file_with_flags(ConstBStringPtr path, Flags flags)
{
    return remove_file_with_flags(path, flags);
}

IOStatus link_file(ConstBStringPtr source_path, ConstBStringPtr target_path)
{
    IOLinkRequest request = IOLinkRequest__new(source_path, target_path);
    return syscall(IO_CALL, IO_LINK, any(&request), 0, 0, 0);
}

IOStatus IO__link_file(ConstBStringPtr source_path, ConstBStringPtr target_path)
{
    return link_file(source_path, target_path);
}

IOStatus mount_device(ConstBStringPtr device_path, ConstBStringPtr filesystem_type, ConstBStringPtr mountpoint)
{
    IOMountRequest request = IOMountRequest__new(device_path, filesystem_type, mountpoint);
    return syscall(IO_CALL, IO_MOUNT, any(&request), 0, 0, 0);
}

IOStatus IO__mount_device(ConstBStringPtr device_path, ConstBStringPtr filesystem_type, ConstBStringPtr mountpoint)
{
    return mount_device(device_path, filesystem_type, mountpoint);
}
