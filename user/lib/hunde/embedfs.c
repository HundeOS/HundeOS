#include "native.h"
#include "filesystem.h"
#include "fs/embedfs.h"

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

HundeIOCompletion hunde_rust_embedfs_execute(void * storage, ConstHundeIORequestV6Ptr input)
{
    match_result(
        IORequest__new_rust(input),
        success(request)
        {
            return HundeIOCompletion__new_reply(input->operation, Embedfs__execute(storage, &request));
        },
        error(code, message)
        {
            return HundeIOCompletion__new_reply(input->operation, IOReply__new(code, 0));
        }
    );
    return HundeIOCompletion__new_reply(input->operation, IOReply__new(RESULT_UNKNOWN_ERROR_CODE, 0));
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
