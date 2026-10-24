#include "filesystem.h"
#include "fs/devfs.h"

RustNativeLayout hunde_rust_devfs_layout(void)
{
    return RustNativeLayout__new(sizeof(FilesystemDriver), _Alignof(FilesystemDriver));
}

void hunde_rust_devfs_init(void * storage)
{
    FilesystemDriverPtr self = storage;
    FilesystemOperations operations = Devfs__operations();
    *self = FilesystemDriver__new((IOServiceContext)DeviceRegistry__shared(), &operations);
}

HundeIOCompletion hunde_rust_devfs_execute(void * storage, ConstHundeIORequestV6Ptr input)
{
    match_result(
        IORequest__new_rust(input),
        success(request)
        {
            return HundeIOCompletion__new_reply(input->operation, FilesystemDriver__execute(storage, &request));
        },
        error(code, message)
        {
            return HundeIOCompletion__new_reply(input->operation, IOReply__new(code, 0));
        }
    );
    return HundeIOCompletion__new_reply(input->operation, IOReply__new(RESULT_UNKNOWN_ERROR_CODE, 0));
}

HundeStatusResult hunde_rust_devfs_disconnect(void * storage, uint64_t session, uint64_t pid)
{
    return FilesystemDriver__release(storage, session, pid) ? HundeStatusResult__success()
                                                            : RustNative__error(IO_UNKNOWN_HANDLE);
}
