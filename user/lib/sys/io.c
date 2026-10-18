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

static BString IO__error_message(IOStatus status)
{
    return BString__new(io_status_string(status));
}

static Result(IOStatus) IO__status_result(IOStatus status)
{
    return status == IO_OK ? Success(IOStatus, IO_OK) : Error(IOStatus, IO__error_message(status), status);
}

static Result(IOHandle) IOStandardStream__open(IOStandardStreamPtr self)
{
    if (self->closed)
    {
        return Error(IOHandle, IO__error_message(IO_NOT_OPEN), IO_NOT_OPEN);
    }
    if (self->handle >= 0)
    {
        return Success(IOHandle, self->handle);
    }
    BString path = self->endpoint == IO_ENDPOINT_TTY_INPUT        ? bstr(":tty:")
                   : self->endpoint == IO_ENDPOINT_KEYBOARD_INPUT ? bstr(":keyboard:")
                                                                  : bstr(":console:");
    self->handle = IOClient__open(&path, self->endpoint == IO_ENDPOINT_CONSOLE_OUTPUT ? IO_OPEN_WRITE : IO_OPEN_READ);
    return self->handle >= 0 ? Success(IOHandle, self->handle)
                             : Error(IOHandle, IO__error_message((IOStatus)self->handle), self->handle);
}

Result(IOStatus) IO__set_terminal_foreground(uint64_t pid)
{
    match_result(
        IOStandardStream__open(&standard_input),
        success(input)
        {
            return IO__status_result(IOClient__set_foreground_process(input, pid));
        },
        error(code, message)
        {
            return Error(IOStatus, message, code);
        }
    );
    return Error(IOStatus, bstr("Unknown error"), RESULT_UNKNOWN_ERROR_CODE);
}

Result(IOTerminalAttributes) IO__get_terminal_attributes(IOHandle handle)
{
    match_optional(
        IO__standard_stream(handle),
        some(stream)
        {
            match_result(
                IOStandardStream__open(stream),
                success(opened)
                {
                    return IO__get_terminal_attributes(opened);
                },
                error(code, message)
                {
                    return Error(IOTerminalAttributes, message, code);
                }
            );
            return Error(IOTerminalAttributes, bstr("Unknown error"), RESULT_UNKNOWN_ERROR_CODE);
        },
        none_do_nothing()
    );
    if (handle < IO_SESSION_HANDLE_BASE)
    {
        return Error(IOTerminalAttributes, IO__error_message(IO_UNKNOWN_HANDLE), IO_UNKNOWN_HANDLE);
    }
    IOTerminalAttributesResult result = IOClient__get_terminal_attributes(handle);
    return result.status == IO_OK ? Success(IOTerminalAttributes, result.attributes)
                                  : Error(IOTerminalAttributes, IO__error_message(result.status), result.status);
}

Result(IOStatus) IO__set_terminal_attributes(IOHandle handle, IOTerminalAttributes attributes)
{
    match_optional(
        IO__standard_stream(handle),
        some(stream)
        {
            match_result(
                IOStandardStream__open(stream),
                success(opened)
                {
                    return IO__set_terminal_attributes(opened, attributes);
                },
                error(code, message)
                {
                    return Error(IOStatus, message, code);
                }
            );
            return Error(IOStatus, bstr("Unknown error"), RESULT_UNKNOWN_ERROR_CODE);
        },
        none_do_nothing()
    );
    return handle < IO_SESSION_HANDLE_BASE ? Error(IOStatus, IO__error_message(IO_UNKNOWN_HANDLE), IO_UNKNOWN_HANDLE)
                                           : IO__status_result(IOClient__set_terminal_attributes(handle, attributes));
}

Result(IOHandle) IO__open(ConstBStringPtr path, Flags flags)
{
    IOHandle handle = IOClient__open(path, flags);
    return handle >= 0 ? Success(IOHandle, handle) : Error(IOHandle, IO__error_message((IOStatus)handle), handle);
}

Result(IOPipe) IO__pipe(void)
{
    return IOClient__pipe();
}

Result(ByteCount) IO__read_bytes_with_flags(IOHandle handle, char * buf, size_t size, Flags flags)
{
    match_optional(
        IO__standard_stream(handle),
        some(stream)
        {
            match_result(
                IOStandardStream__open(stream),
                success(opened)
                {
                    return IO__read_bytes_with_flags(opened, buf, size, flags);
                },
                error(code, message)
                {
                    return Error(ByteCount, message, code);
                }
            );
            return Error(ByteCount, bstr("Unknown error"), RESULT_UNKNOWN_ERROR_CODE);
        },
        none_do_nothing()
    );
    if (handle >= IO_SESSION_HANDLE_BASE)
    {
        ByteCount count = IOClient__read_with_flags(handle, (ChannelDataPtr)buf, size, flags);
        return count >= 0 ? Success(ByteCount, count) : Error(ByteCount, IO__error_message((IOStatus)count), count);
    }
    return Error(ByteCount, IO__error_message(IO_UNKNOWN_HANDLE), IO_UNKNOWN_HANDLE);
}

Result(ByteCount) IO__read_bytes(IOHandle handle, char * buf, size_t size)
{
    return IO__read_bytes_with_flags(handle, buf, size, 0);
}

Result(ByteCount) IO__write_bytes_with_flags(IOHandle handle, const char * buf, size_t size, Flags flags)
{
    match_optional(
        IO__standard_stream(handle),
        some(stream)
        {
            match_result(
                IOStandardStream__open(stream),
                success(opened)
                {
                    return IO__write_bytes_with_flags(opened, buf, size, flags);
                },
                error(code, message)
                {
                    return Error(ByteCount, message, code);
                }
            );
            return Error(ByteCount, bstr("Unknown error"), RESULT_UNKNOWN_ERROR_CODE);
        },
        none_do_nothing()
    );
    if (handle >= IO_SESSION_HANDLE_BASE)
    {
        ByteCount count = IOClient__write_with_flags(handle, (ConstChannelDataPtr)buf, size, flags);
        return count >= 0 ? Success(ByteCount, count) : Error(ByteCount, IO__error_message((IOStatus)count), count);
    }
    return Error(ByteCount, IO__error_message(IO_UNKNOWN_HANDLE), IO_UNKNOWN_HANDLE);
}

Result(ByteCount) IO__write_bytes(IOHandle handle, const char * buf, size_t size)
{
    return IO__write_bytes_with_flags(handle, buf, size, 0);
}

Result(IOPosition) IO__seek(IOHandle handle, int64_t offset, IOSeekWhence whence)
{
    if (handle < IO_SESSION_HANDLE_BASE)
    {
        return Error(IOPosition, IO__error_message(IO_OPERATION_NOT_SUPPORTED), IO_OPERATION_NOT_SUPPORTED);
    }
    IOReply reply = IOClient__seek(handle, offset, whence);
    return reply.status == IO_OK ? Success(IOPosition, reply.value)
                                 : Error(IOPosition, IO__error_message(reply.status), reply.status);
}

Result(IOFileChange) IO__watch_file(IOHandle handle)
{
    if (handle < IO_SESSION_HANDLE_BASE)
    {
        return Error(IOFileChange, IO__error_message(IO_OPERATION_NOT_SUPPORTED), IO_OPERATION_NOT_SUPPORTED);
    }
    IOReply reply = IOClient__watch_file(handle);
    return reply.status == IO_OK ? Success(IOFileChange, reply.file_change)
                                 : Error(IOFileChange, IO__error_message(reply.status), reply.status);
}

Result(IOFileChange) IO__wait_file_change(IOFileWatch * watches, size_t count)
{
    IOReply reply = IOClient__wait_file_change(watches, count);
    return reply.status == IO_OK ? Success(IOFileChange, reply.file_change)
                                 : Error(IOFileChange, IO__error_message(reply.status), reply.status);
}

Result(IOHandle) accept(IOHandle handle)
{
    if (handle >= IO_SESSION_HANDLE_BASE)
    {
        IOHandle accepted = IOClient__accept(handle);
        return accepted >= 0 ? Success(IOHandle, accepted)
                             : Error(IOHandle, IO__error_message((IOStatus)accepted), accepted);
    }
    return Error(IOHandle, IO__error_message(IO_UNKNOWN_HANDLE), IO_UNKNOWN_HANDLE);
}

Result(IOHandle) IO__accept(IOHandle handle)
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

static Result(ByteCount) IO__read_bstring_with_limit(IOHandle handle, BStringPtr buffer, StringPos max_bytes)
{
    BString__clear(buffer);
    if (max_bytes <= 0)
    {
        return Success(ByteCount, 0);
    }

    StringPos buffer_max = BSTRING_MAX_SIZE - 1;
    StringPos read_max = max_bytes < buffer_max ? max_bytes : buffer_max;

    match_result(
        IO__read_bytes(handle, (char *)BString__cstr(buffer), (size_t)read_max),
        success(bytes)
        {
            ByteCount bounded_bytes = bytes > read_max ? read_max : bytes;
            *buffer = BString__from_chars(BString__cstr(buffer), bounded_bytes);
            return Success(ByteCount, bounded_bytes);
        },
        error(code, message)
        {
            return Error(ByteCount, message, code);
        }
    );
    return Error(ByteCount, bstr("Unknown error"), RESULT_UNKNOWN_ERROR_CODE);
}

Result(ByteCount) IO__read(IOHandle handle, BStringPtr buffer)
{
    return IO__read_bstring_with_limit(handle, buffer, BSTRING_MAX_SIZE - 1);
}

Result(DynString) IO__read_dyn_string(IOHandle handle, StringPos max_bytes)
{
    DynString text;
    match_result(
        DynString__init(&text, max_bytes),
        success_do_nothing(),
        error(code, message)
        {
            return Error(DynString, message, code);
        }
    );

    ByteCount total_read = 0;
    while (total_read < max_bytes)
    {
        BString chunk = empty_bstr();
        ByteCount remaining_capacity = max_bytes - total_read;
        match_result(
            IO__read_bstring_with_limit(handle, &chunk, remaining_capacity),
            success(bytes)
            {
                if (bytes == 0)
                {
                    return Success(DynString, text);
                }
                match_result(
                    DynString__append_bstring(&text, &chunk),
                    success_do_nothing(),
                    error(code, message)
                    {
                        DynString__dispose(&text);
                        return Error(DynString, message, code);
                    }
                );

                total_read += bytes;
                if (total_read >= max_bytes || bytes < BSTRING_MAX_SIZE - 1)
                {
                    return Success(DynString, text);
                }
            },
            error(code, message)
            {
                if (code == IO_EOF)
                {
                    return Success(DynString, text);
                }
                DynString__dispose(&text);
                return Error(DynString, message, code);
            }
        );
    }

    return Success(DynString, text);
}

Result(ByteCount) IO__write(IOHandle handle, ConstBStringPtr text)
{
    return IO__write_bytes(handle, BString__cstr(text), (size_t)BString__length(text));
}

Result(IOStatus) IO__close(IOHandle handle)
{
    match_optional(
        IO__standard_stream(handle),
        some(stream)
        {
            if (stream->closed)
            {
                return Error(IOStatus, IO__error_message(IO_NOT_OPEN), IO_NOT_OPEN);
            }
            if (stream->handle < 0)
            {
                stream->closed = true;
                return Success(IOStatus, IO_OK);
            }
            match_result(
                IO__close(stream->handle),
                success(status)
                {
                    stream->closed = true;
                    return Success(IOStatus, status);
                },
                error(code, message)
                {
                    return Error(IOStatus, message, code);
                }
            );
            return Error(IOStatus, bstr("Unknown error"), RESULT_UNKNOWN_ERROR_CODE);
        },
        none_do_nothing()
    );
    if (handle >= IO_SESSION_HANDLE_BASE)
    {
        return IO__status_result(IOClient__close(handle));
    }
    return Error(IOStatus, IO__error_message(IO_UNKNOWN_HANDLE), IO_UNKNOWN_HANDLE);
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

Result(DirectoryEntryCount) list_dir(ConstBStringPtr path, DirectoryEntryPtr entries, size_t max_entries)
{
    if (max_entries == 0)
    {
        return Success(DirectoryEntryCount, 0);
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
                return Success(DirectoryEntryCount, (DirectoryEntryCount)count);
            }
            return Error(DirectoryEntryCount, IO__error_message(reply.status), reply.status);
        }
        if (reply.directory.count > IO_DIRECTORY_BATCH_CAPACITY)
        {
            return Error(DirectoryEntryCount, IO__error_message(IO_UNKNOWN_BUFFER), IO_UNKNOWN_BUFFER);
        }
        if (reply.directory.count == 0)
        {
            return Success(DirectoryEntryCount, (DirectoryEntryCount)count);
        }
        for (uint64_t index = 0; index < reply.directory.count && count < max_entries; index++)
        {
            DirectoryEntry entry = reply.directory.entries[index];
            if (entry.name_len >= IO_DIRENT_NAME_MAX)
            {
                return Error(DirectoryEntryCount, IO__error_message(IO_UNKNOWN_BUFFER), IO_UNKNOWN_BUFFER);
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
    return Success(DirectoryEntryCount, (DirectoryEntryCount)count);
}

Result(DirectoryEntryCount) IO__list_dir(ConstBStringPtr path, DirectoryEntryPtr entries, size_t max_entries)
{
    return list_dir(path, entries, max_entries);
}

Result(IOFileInfo) IO__file_info(ConstBStringPtr path)
{
    IOReply reply = IOClient__path_request(IO_REQUEST_FILE_INFO, path, 0, 0);
    return reply.status == IO_OK ? Success(IOFileInfo, reply.file_info)
                                 : Error(IOFileInfo, IO__error_message(reply.status), reply.status);
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

Result(IOStatus) make_dir(ConstBStringPtr path)
{
    return IO__status_result(IOClient__path_request(IO_REQUEST_MKDIR, path, 0, 0).status);
}

Result(IOStatus) IO__make_dir(ConstBStringPtr path)
{
    return make_dir(path);
}

Result(IOStatus) remove_file(ConstBStringPtr path)
{
    return remove_file_with_flags(path, 0);
}

Result(IOStatus) IO__remove_file(ConstBStringPtr path)
{
    return remove_file(path);
}

Result(IOStatus) remove_file_with_flags(ConstBStringPtr path, Flags flags)
{
    return IO__status_result(IOClient__path_request(IO_REQUEST_REMOVE, path, flags, 0).status);
}

Result(IOStatus) IO__remove_file_with_flags(ConstBStringPtr path, Flags flags)
{
    return remove_file_with_flags(path, flags);
}

Result(IOStatus) link_file(ConstBStringPtr source_path, ConstBStringPtr target_path)
{
    return IO__status_result(IOClient__link(source_path, target_path));
}

Result(IOStatus) IO__link_file(ConstBStringPtr source_path, ConstBStringPtr target_path)
{
    return link_file(source_path, target_path);
}
