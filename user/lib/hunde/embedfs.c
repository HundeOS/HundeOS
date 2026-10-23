#include "native.h"
#include "fs/embedfs.h"
#include "memory.h"

RustNativeLayout hunde_rust_embedfs_layout(void)
{
    return RustNativeLayout__new(sizeof(Embedfs), _Alignof(Embedfs));
}

HundeStatusResult hunde_rust_embedfs_init(void * storage)
{
    EmbeddedFsHeaderPtr header = (EmbeddedFsHeaderPtr)EMBEDDED_FS_VADDR;
    if (header->blob_size > EMBEDDED_FS_MAX_SIZE)
    {
        return RustNative__error(IO_INVALID_PATH);
    }
    EmbeddedFs archive = EmbeddedFs__new(EMBEDDED_FS_VADDR, EMBEDDED_FS_VADDR + header->blob_size);
    if (!EmbeddedFs__is_valid(&archive))
    {
        return RustNative__error(IO_INVALID_PATH);
    }
    Embedfs__init(storage, &archive);
    return HundeStatusResult__success();
}

static HundeIOFileInfo HundeIOFileInfo__new(IOFileInfo info)
{
    return (HundeIOFileInfo){.mode = info.mode,
                             .user_id = info.user_id,
                             .group_id = info.group_id,
                             .size = info.size,
                             .identity = info.identity};
}

static HundeIOCompletion HundeIOCompletion__new(IOReplyPtr reply)
{
    HundeIOCompletion completion = {.kind = reply->status == IO_OK ? HUNDE_ABI_SUCCESS : HUNDE_ABI_ERROR,
                                    .value = reply->value,
                                    .file_info = HundeIOFileInfo__new(reply->file_info),
                                    .directory_count = reply->directory.count};
    completion.error =
        HundeStatusResult__error(reply->status, HundeByteSpan__new((const uint8_t *)"Archive I/O error", 17)).error;
    for (uint64_t index = 0; index < reply->directory.count; index++)
    {
        DirectoryEntry * entry = &reply->directory.entries[index];
        completion.directory[index].info.mode = entry->mode;
        completion.directory[index].info.user_id = entry->user_id;
        completion.directory[index].info.group_id = entry->group_id;
        BString name = BString__from_chars(entry->name, entry->name_len);
        completion.directory[index].name_length =
            BString__copy_to(&name, (char *)completion.directory[index].name, sizeof(completion.directory[index].name));
    }
    return completion;
}

HundeIOCompletion hunde_rust_embedfs_execute(void * storage, ConstHundeIORequestV6Ptr input)
{
    IORequest request = IORequest__new(input->operation, input->handle);
    request.client_session = input->session;
    request.client_pid = input->pid;
    request.flags = input->flags;
    request.count = input->count;
    request.offset = input->offset;
    if (input->count > input->capacity || input->count > BUFFER_SIZE ||
        (input->operation != IO_REQUEST_READ && input->count > sizeof(request.data)))
    {
        IOReply failure = IOReply__new(IO_UNKNOWN_BUFFER, 0);
        return HundeIOCompletion__new(&failure);
    }
    if (input->operation == IO_REQUEST_READ)
    {
        request.transfer_address = (Address)input->read_data;
    }
    else if (input->count > 0)
    {
        Mem__memcpy(request.data, input->write_data, input->count);
    }
    IOReply reply = Embedfs__execute(storage, &request);
    return HundeIOCompletion__new(&reply);
}

HundeStatusResult hunde_rust_embedfs_disconnect(void * storage, uint64_t session)
{
    match_result(
        Embedfs__release_session(storage, session),
        success(code)
        {
            return HundeStatusResult__success();
        },
        error(code, message)
        {
            return RustNative__error(code);
        }
    );
    return RustNative__unknown_error();
}
