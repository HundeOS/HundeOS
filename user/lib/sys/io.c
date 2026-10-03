#include "io.h"

#include <stddef.h>

#include "io/client.h"

#include "io/path.h"

typedef struct IOStandardStream
{
    IOHandle endpoint;
    IOHandle handle;
    bool closed;
} IOStandardStream;

typedef IOStandardStream * IOStandardStreamPtr;
OptionalDef(IOStandardStreamPtr);
static IOStandardStream standard_input;
static IOStandardStream standard_output;
static IOStandardStream standard_error;

static IOStandardStream IOStandardStream__new(IOHandle endpoint)
{
    return (IOStandardStream){.endpoint = endpoint, .handle = IO_NOT_OPEN};
}

void IO__set_startup_stdio(IOHandle input, IOHandle output, IOHandle error)
{
    standard_input = IOStandardStream__new(input);
    standard_output = IOStandardStream__new(output);
    standard_error = IOStandardStream__new(error);
}

void IO__reset_after_fork(void)
{
    standard_input.handle = IO_NOT_OPEN;
    standard_output.handle = IO_NOT_OPEN;
    standard_error.handle = IO_NOT_OPEN;
}

static Optional(IOStandardStreamPtr) IO__standard_stream(IOHandle handle)
{
    switch (handle)
    {
    case STDIN_FILENO:
        if (IOEndpoint__is_terminal(standard_input.endpoint))
        {
            return Some(IOStandardStreamPtr, &standard_input);
        }
        break;
    case STDOUT_FILENO:
        if (IOEndpoint__is_terminal(standard_output.endpoint))
        {
            return Some(IOStandardStreamPtr, &standard_output);
        }
        break;
    case STDERR_FILENO:
        if (IOEndpoint__is_terminal(standard_error.endpoint))
        {
            return Some(IOStandardStreamPtr, &standard_error);
        }
        break;
    }
    return None(IOStandardStreamPtr);
}

Optional(BString) IO__terminal_path(int descriptor)
{
    match_optional(
        IO__standard_stream(descriptor),
        some(stream)
        {
            if (stream->closed)
            {
                return None(BString);
            }
            return Some(BString, stream->endpoint == IO_ENDPOINT_TTY_INPUT ? bstr(":tty:") : bstr(":console:"));
        },
        none()
        {
            return None(BString);
        }
    );
}

static IOHandle IOStandardStream__open(IOStandardStreamPtr self)
{
    if (self->closed)
    {
        return IO_NOT_OPEN;
    }
    if (self->handle >= 0)
    {
        return self->handle;
    }
    BString path = self->endpoint == IO_ENDPOINT_TTY_INPUT        ? bstr(":tty:")
                   : self->endpoint == IO_ENDPOINT_KEYBOARD_INPUT ? bstr(":keyboard:")
                                                                  : bstr(":console:");
    self->handle = IO__open(&path, self->endpoint == IO_ENDPOINT_CONSOLE_OUTPUT ? IO_OPEN_WRITE : IO_OPEN_READ);
    return self->handle;
}

IOStatus IO__set_terminal_foreground(uint64_t pid)
{
    IOHandle input = IOStandardStream__open(&standard_input);
    if (input < 0)
    {
        return (IOStatus)input;
    }
    return IOClient__set_foreground_process(input, pid);
}

IOTerminalAttributesResult IO__get_terminal_attributes(IOHandle handle)
{
    match_optional(
        IO__standard_stream(handle),
        some(stream)
        {
            IOHandle opened = IOStandardStream__open(stream);
            if (opened < 0)
            {
                return IOTerminalAttributesResult__new((IOStatus)opened, IOTerminalAttributes__default());
            }
            return IO__get_terminal_attributes(opened);
        },
        none_do_nothing()
    );
    if (handle < IO_SESSION_HANDLE_BASE)
    {
        return IOTerminalAttributesResult__new(IO_UNKNOWN_HANDLE, IOTerminalAttributes__default());
    }
    return IOClient__get_terminal_attributes(handle);
}

IOStatus IO__set_terminal_attributes(IOHandle handle, IOTerminalAttributes attributes)
{
    match_optional(
        IO__standard_stream(handle),
        some(stream)
        {
            IOHandle opened = IOStandardStream__open(stream);
            return opened < 0 ? (IOStatus)opened : IO__set_terminal_attributes(opened, attributes);
        },
        none_do_nothing()
    );
    return handle < IO_SESSION_HANDLE_BASE ? IO_UNKNOWN_HANDLE : IOClient__set_terminal_attributes(handle, attributes);
}

IOHandle IO__open(ConstBStringPtr path, Flags flags)
{
    return IOClient__open(path, flags);
}

Result(IOPipe) IO__pipe(void)
{
    return IOClient__pipe();
}

ByteCount IO__read_bytes_with_flags(IOHandle handle, char * buf, size_t size, Flags flags)
{
    match_optional(
        IO__standard_stream(handle),
        some(stream)
        {
            IOHandle opened = IOStandardStream__open(stream);
            return opened < 0 ? opened : IO__read_bytes_with_flags(opened, buf, size, flags);
        },
        none_do_nothing()
    );
    if (handle >= IO_SESSION_HANDLE_BASE)
    {
        return IOClient__read_with_flags(handle, (ChannelDataPtr)buf, size, flags);
    }
    return IO_UNKNOWN_HANDLE;
}

ByteCount IO__read_bytes(IOHandle handle, char * buf, size_t size)
{
    return IO__read_bytes_with_flags(handle, buf, size, 0);
}

ByteCount IO__write_bytes_with_flags(IOHandle handle, const char * buf, size_t size, Flags flags)
{
    match_optional(
        IO__standard_stream(handle),
        some(stream)
        {
            IOHandle opened = IOStandardStream__open(stream);
            return opened < 0 ? opened : IO__write_bytes_with_flags(opened, buf, size, flags);
        },
        none_do_nothing()
    );
    if (handle >= IO_SESSION_HANDLE_BASE)
    {
        return IOClient__write_with_flags(handle, (ConstChannelDataPtr)buf, size, flags);
    }
    return IO_UNKNOWN_HANDLE;
}

ByteCount IO__write_bytes(IOHandle handle, const char * buf, size_t size)
{
    return IO__write_bytes_with_flags(handle, buf, size, 0);
}

IOSeekResult IO__seek(IOHandle handle, int64_t offset, IOSeekWhence whence)
{
    if (handle < IO_SESSION_HANDLE_BASE)
    {
        return IOSeekResult__new(IO_OPERATION_NOT_SUPPORTED, 0);
    }
    IOReply reply = IOClient__seek(handle, offset, whence);
    return IOSeekResult__new(reply.status, reply.value);
}

IOWatchResult IO__watch_file(IOHandle handle)
{
    if (handle < IO_SESSION_HANDLE_BASE)
    {
        return IOWatchResult__new(IO_OPERATION_NOT_SUPPORTED, IOFileChange__new(0, 0, 0));
    }
    IOReply reply = IOClient__watch_file(handle);
    return IOWatchResult__new(reply.status, reply.file_change);
}

IOWatchResult IO__wait_file_change(IOFileWatch * watches, size_t count)
{
    IOReply reply = IOClient__wait_file_change(watches, count);
    return IOWatchResult__new(reply.status, reply.file_change);
}

IOHandle accept(IOHandle handle)
{
    if (handle >= IO_SESSION_HANDLE_BASE)
    {
        return IOClient__accept(handle);
    }
    return IO_UNKNOWN_HANDLE;
}

IOHandle IO__accept(IOHandle handle)
{
    return accept(handle);
}

BString IO__path_from_arg(ConstBStringPtr arg)
{
    BString result = bstr("/");
    StringPos cursor = 0;
    while (cursor < BString__length(arg))
    {
        while (cursor < BString__length(arg) && BString__char_at_or(arg, cursor, '\0') == '/')
        {
            cursor++;
        }
        StringPos component_start = cursor;
        while (cursor < BString__length(arg) && BString__char_at_or(arg, cursor, '\0') != '/')
        {
            cursor++;
        }
        StringPos component_length = cursor - component_start;
        if (component_length == 0 || (component_length == 1 && BString__char_at_or(arg, component_start, '\0') == '.'))
        {
            continue;
        }
        if (component_length == 2 && BString__char_at_or(arg, component_start, '\0') == '.' &&
            BString__char_at_or(arg, component_start + 1, '\0') == '.')
        {
            while (BString__length(&result) > 1 &&
                   BString__char_at_or(&result, BString__length(&result) - 1, '\0') != '/')
            {
                BString__remove_at(&result, BString__length(&result) - 1);
            }
            if (BString__length(&result) > 1)
            {
                BString__remove_at(&result, BString__length(&result) - 1);
            }
            continue;
        }

        if (BString__length(&result) > 1)
        {
            BString__push_char(&result, '/');
        }
        for (StringPos index = 0; index < component_length; index++)
        {
            BString__push_char(&result, BString__char_at_or(arg, component_start + index, '\0'));
        }
    }
    return result;
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

    ByteCount bytes = IO__read_bytes(handle, (char *)BString__cstr(buffer), (size_t)read_max);
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
        error(code, message)
        {
            return None(DynString);
        }
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
    return IO__write_bytes(handle, BString__cstr(text), (size_t)BString__length(text));
}

IOStatus IO__close(IOHandle handle)
{
    match_optional(
        IO__standard_stream(handle),
        some(stream)
        {
            if (stream->closed)
            {
                return IO_NOT_OPEN;
            }
            IOStatus status = stream->handle >= 0 ? IO__close(stream->handle) : IO_OK;
            if (status == IO_OK)
            {
                stream->closed = true;
            }
            return status;
        },
        none_do_nothing()
    );
    if (handle >= IO_SESSION_HANDLE_BASE)
    {
        return IOClient__close(handle);
    }
    return IO_UNKNOWN_HANDLE;
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
    if (max_entries == 0)
    {
        return 0;
    }
    size_t count = 0;
    uint64_t offset = 0;
    while (count < max_entries)
    {
        IOReply reply = IOClient__path_request(IO_REQUEST_LIST_DIR, path, 0, offset);
        if (reply.status != IO_OK)
        {
            if (reply.status == IO_INVALID_PATH && count > 0)
            {
                return (DirectoryEntryCount)count;
            }
            return reply.status;
        }
        if (reply.directory.count > IO_DIRECTORY_BATCH_CAPACITY)
        {
            return IO_UNKNOWN_BUFFER;
        }
        if (reply.directory.count == 0)
        {
            return (DirectoryEntryCount)count;
        }
        for (uint64_t index = 0; index < reply.directory.count && count < max_entries; index++)
        {
            DirectoryEntry entry = reply.directory.entries[index];
            if (entry.name_len >= IO_DIRENT_NAME_MAX)
            {
                return IO_UNKNOWN_BUFFER;
            }
            BString name = BString__from_chars(entry.name, entry.name_len);
            // Namespace overlays arrive first and take precedence over disk entries.
            if (!IO__directory_contains(entries, count, &name))
            {
                entries[count++] = entry;
            }
        }
        offset += reply.directory.count;
    }
    return (DirectoryEntryCount)count;
}

DirectoryEntryCount IO__list_dir(ConstBStringPtr path, DirectoryEntryPtr entries, size_t max_entries)
{
    return list_dir(path, entries, max_entries);
}

Result(IOFileInfo) IO__file_info(ConstBStringPtr path)
{
    IOReply reply = IOClient__path_request(IO_REQUEST_FILE_INFO, path, 0, 0);
    return reply.status == IO_OK ? Success(IOFileInfo, reply.file_info)
                                 : Error(IOFileInfo, bstr("File information unavailable"), reply.status);
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
    case IO_INTERRUPTED:
        return "IO_INTERRUPTED";
    case IO_ALREADY_EXISTS:
        return "IO_ALREADY_EXISTS";
    case IO_REDIRECT:
        return "IO_REDIRECT";
    case IO_BROKEN_PIPE:
        return "IO_BROKEN_PIPE";
    case IO_WOULD_BLOCK:
        return "IO_WOULD_BLOCK";
    case IO_PANIC:
        return "IO_PANIC";
    }

    return "IO_UNKNOWN_ERROR";
}

IOStatus make_dir(ConstBStringPtr path)
{
    return IOClient__path_request(IO_REQUEST_MKDIR, path, 0, 0).status;
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
    return IOClient__path_request(IO_REQUEST_REMOVE, path, flags, 0).status;
}

IOStatus IO__remove_file_with_flags(ConstBStringPtr path, Flags flags)
{
    return remove_file_with_flags(path, flags);
}

IOStatus link_file(ConstBStringPtr source_path, ConstBStringPtr target_path)
{
    return IOClient__link(source_path, target_path);
}

IOStatus IO__link_file(ConstBStringPtr source_path, ConstBStringPtr target_path)
{
    return link_file(source_path, target_path);
}
