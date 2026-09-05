#include "io.h"

#include <stddef.h>

#include "syscall.h"

IOHandle open(ConstBStringPtr path, Flags flags)
{
    IOPathRequest request = IOPathRequest__new(path, flags);
    return syscall(IO_CALL, IO_OPEN, any(&request), 0, 0, 0);
}

ByteCount read(IOHandle handle, char *buf, size_t size)
{
    return syscall(IO_CALL, IO_READ, handle, any(buf), size, 0);
}

ByteCount write(IOHandle handle, char *buf, size_t size)
{
    return syscall(IO_CALL, IO_WRITE, handle, any(buf), size, 0);
}

IOHandle IO__open(ConstBStringPtr path, Flags flags)
{
    return open(path, flags);
}

IOHandle accept(IOHandle handle)
{
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

    ByteCount bytes = read(handle, (char *) BString__cstr(buffer), (size_t) read_max);
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
    match_result(DynStringCode, DynString__init(&text, max_bytes),
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

        match_result(DynStringCode, DynString__append_bstring(&text, &chunk),
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
    return write(handle, (char *) BString__cstr(text), (size_t) BString__length(text));
}

IOStatus close(IOHandle handle)
{
    IOStatus result = (IOStatus) syscall(IO_CALL, IO_CLOSE, handle, 0, 0, 0);
    if (result > 0)
    {
        return IO_OK;
    }

    return result;
}

DirectoryEntryCount list_dir(ConstBStringPtr path, DirectoryEntryPtr entries, size_t max_entries)
{
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
        case IO_PANIC:
            return "IO_PANIC";
    }

    return "IO_UNKNOWN_ERROR";
}

IOStatus make_dir(ConstBStringPtr path)
{
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
