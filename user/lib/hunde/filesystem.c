#include "filesystem.h"
#include "memory.h"

Result(IORequest) IORequest__new_rust(ConstHundeIORequestV6Ptr input)
{
    if (input->operation == IO_REQUEST_GET_TERMINAL_ATTRIBUTES ||
        input->operation == IO_REQUEST_SET_TERMINAL_ATTRIBUTES || input->operation == IO_REQUEST_SET_FOREGROUND_PROCESS)
    {
        return Error(IORequest, bstr("Filesystem does not provide terminal controls"), IO_OPERATION_NOT_SUPPORTED);
    }
    if (input->count > input->capacity || input->count > BUFFER_SIZE)
    {
        return Error(IORequest, bstr("Invalid native filesystem buffer"), IO_UNKNOWN_BUFFER);
    }
    IORequest request = IORequest__new(input->operation, input->handle);
    request.client_session = input->session;
    request.client_pid = input->pid;
    request.flags = input->flags;
    request.count = input->count;
    request.offset = input->offset;
    request.inheritance_token = input->inheritance_token;
    request.inheritance_pid = input->inheritance_pid;
    request.buffered = input->buffered != 0;
    if (input->operation == IO_REQUEST_READ)
    {
        request.transfer_address = (Address)input->read_data;
    }
    else if (input->operation == IO_REQUEST_WRITE)
    {
        request.transfer_address = (Address)input->write_data;
    }
    else if (input->operation == IO_REQUEST_LINK)
    {
        if (input->offset == 0 || input->offset >= input->count || input->offset >= sizeof(request.data) ||
            input->count - input->offset >= sizeof(request.target_path_data))
        {
            return Error(IORequest, bstr("Invalid native filesystem link"), IO_INVALID_PATH);
        }
        request.count = input->offset;
        request.target_path_length = input->count - input->offset;
        Mem__memcpy(request.data, input->write_data, request.count);
        Mem__memcpy(request.target_path_data, input->write_data + input->offset, request.target_path_length);
    }
    else
    {
        if (input->count > sizeof(request.data))
        {
            return Error(IORequest, bstr("Invalid native filesystem path"), IO_INVALID_PATH);
        }
        if (input->count > 0)
        {
            Mem__memcpy(request.data, input->write_data, input->count);
        }
    }
    return Success(IORequest, request);
}

static HundeIOFileInfo HundeIOFileInfo__new(IOFileInfo info)
{
    return (HundeIOFileInfo){.mode = info.mode,
                             .user_id = info.user_id,
                             .group_id = info.group_id,
                             .size = info.size,
                             .identity = info.identity};
}

HundeIOCompletion HundeIOCompletion__new_reply(IORequestOperation operation, IOReply reply)
{
    if (reply.status == IO_WAIT_FOR_DATA)
    {
        return (HundeIOCompletion){.kind = HUNDE_IO_PENDING};
    }
    if (reply.status != IO_OK)
    {
        return (HundeIOCompletion){.kind = HUNDE_ABI_ERROR, .error = RustNative__error(reply.status).error};
    }
    HundeIOCompletion completion = {.kind = HUNDE_ABI_SUCCESS,
                                    .value = reply.value,
                                    .pipe_read = reply.pipe.read,
                                    .pipe_write = reply.pipe.write,
                                    .inheritance_flags = reply.inheritance_flags};
    if (operation == IO_REQUEST_FILE_INFO)
    {
        completion.file_info = HundeIOFileInfo__new(reply.file_info);
    }
    if (operation == IO_REQUEST_WATCH_FILE || operation == IO_REQUEST_WAIT_FILE_CHANGE)
    {
        completion.file_change = reply.file_change;
    }
    if (operation == IO_REQUEST_LIST_DIR)
    {
        if (reply.directory.count > IO_DIRECTORY_BATCH_CAPACITY)
        {
            return (HundeIOCompletion){.kind = HUNDE_ABI_ERROR, .error = RustNative__error(IO_UNKNOWN_BUFFER).error};
        }
        completion.directory_count = reply.directory.count;
        for (uint64_t index = 0; index < reply.directory.count; index++)
        {
            DirectoryEntry * entry = &reply.directory.entries[index];
            if (entry->name_len >= IO_DIRENT_NAME_MAX)
            {
                return (HundeIOCompletion){.kind = HUNDE_ABI_ERROR,
                                           .error = RustNative__error(IO_UNKNOWN_BUFFER).error};
            }
            completion.directory[index].info.mode = entry->mode;
            completion.directory[index].info.user_id = entry->user_id;
            completion.directory[index].info.group_id = entry->group_id;
            BString name = BString__from_chars(entry->name, entry->name_len);
            completion.directory[index].name_length = BString__copy_to(&name, (char *)completion.directory[index].name,
                                                                       sizeof(completion.directory[index].name));
        }
    }
    return completion;
}
