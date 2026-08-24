#include "io.h"

#include <stddef.h>

#include "syscall.h"

IOHandle open(const char *path, Flags flags)
{
    return syscall(IO_CALL, IO_OPEN, any(path), flags, 0, 0);
}

ByteCount read(IOHandle handle, char *buf, size_t size)
{
    return syscall(IO_CALL, IO_READ, handle, any(buf), size, 0);
}

ByteCount write(IOHandle handle, char *buf, size_t size)
{
    return syscall(IO_CALL, IO_WRITE, handle, any(buf), size, 0);
}

IOHandle IO__open(ConstBStringPtr self, Flags flags)
{
    return open(BString__cstr(self), flags);
}

BString IO__path_from_arg(ConstBStringPtr arg)
{
    BString slash = BSTRING_NEW("/");
    if (BString__is_empty(arg))
    {
        return slash;
    }

    if (BString__starts_with(arg, &slash))
    {
        return *arg;
    }

    return BString__append(&slash, arg);
}

BString IO__path_join(ConstBStringPtr base, ConstBStringPtr child)
{
    BString slash = BSTRING_NEW("/");
    if (BString__is_empty(base) || BString__equals(base, &slash))
    {
        return BString__append(&slash, child);
    }

    BString with_slash = BString__append(base, &slash);
    return BString__append(&with_slash, child);
}

static ByteCount IO__read_bstring_with_limit(IOHandle handle, BStringPtr self, StringPos max_bytes)
{
    BString__clear(self);
    if (max_bytes <= 0)
    {
        return 0;
    }

    StringPos buffer_max = BSTRING_MAX_SIZE - 1;
    StringPos read_max = max_bytes < buffer_max ? max_bytes : buffer_max;

    ByteCount bytes = read(handle, (char *) BString__cstr(self), (size_t) read_max);
    if (bytes <= 0)
    {
        return bytes;
    }

    if (bytes > read_max)
    {
        bytes = read_max;
    }

    *self = BString__from_chars(BString__cstr(self), bytes);
    return bytes;
}

ByteCount IO__read(IOHandle handle, BStringPtr self)
{
    return IO__read_bstring_with_limit(handle, self, BSTRING_MAX_SIZE - 1);
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
        BString chunk = EMPTY_BSTRING;
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

ByteCount IO__write(IOHandle handle, ConstBStringPtr self)
{
    return write(handle, (char *) BString__cstr(self), (size_t) BString__length(self));
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

DirectoryEntryCount list_dir(const char * path, DirectoryEntryPtr entries, size_t max_entries)
{
    return syscall(IO_CALL, IO_LIST_DIR, any(path), any(entries), max_entries, 0);
}

DirectoryEntryCount IO__list_dir(ConstBStringPtr self, DirectoryEntryPtr entries, size_t max_entries)
{
    return list_dir(BString__cstr(self), entries, max_entries);
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

IOStatus make_dir(const char * path)
{
    return syscall(IO_CALL, IO_MKDIR, any(path), 0, 0, 0);
}

IOStatus IO__make_dir(ConstBStringPtr self)
{
    return make_dir(BString__cstr(self));
}

IOStatus remove_file(const char * path)
{
    return remove_file_with_flags(path, 0);
}

IOStatus IO__remove_file(ConstBStringPtr self)
{
    return remove_file(BString__cstr(self));
}

IOStatus remove_file_with_flags(const char * path, Flags flags)
{
    return syscall(IO_CALL, IO_REMOVE, any(path), flags, 0, 0);
}

IOStatus IO__remove_file_with_flags(ConstBStringPtr self, Flags flags)
{
    return remove_file_with_flags(BString__cstr(self), flags);
}

IOStatus link_file(const char * source_path, const char * target_path)
{
    return syscall(IO_CALL, IO_LINK, any(source_path), any(target_path), 0, 0);
}

IOStatus IO__link_file(ConstBStringPtr source_path, ConstBStringPtr target_path)
{
    return link_file(BString__cstr(source_path), BString__cstr(target_path));
}

IOStatus mount_device(const char * device_path, const char * filesystem_type, const char * mountpoint)
{
    return syscall(IO_CALL, IO_MOUNT, any(device_path), any(filesystem_type), any(mountpoint), 0);
}

IOStatus IO__mount_device(ConstBStringPtr device_path, ConstBStringPtr filesystem_type, ConstBStringPtr mountpoint)
{
    return mount_device(BString__cstr(device_path), BString__cstr(filesystem_type), BString__cstr(mountpoint));
}
