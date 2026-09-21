#include "mount_client.h"
#include "protocol.h"
#include <string.h>

Result(IOStatus) MountClient__add(ChannelPtr self, MountConfig config)
{
    BString device = BString__is_empty(&config.device_path) ? bstr("-") : config.device_path;
    if (BString__length(&device) + BString__length(&config.filesystem_type) + BString__length(&config.mountpoint) + 9 >=
        IO_PROTOCOL_DATA_SIZE)
    {
        return Error(IOStatus, bstr("Mount request too long"), IO_INVALID_PATH);
    }
    BString line =
        BString__sprintf(&bstr("device %s %s %s"), cstr(device), cstr(config.filesystem_type), cstr(config.mountpoint));
    IORequest request = IORequest__new(IO_REQUEST_MOUNT, 0);
    request.count = BString__length(&line);
    memcpy(request.data, BString__cstr(&line), request.count);
    IOReply reply;
    match_result(
        Channel__submit(self, (ConstChannelDataPtr)&request, sizeof(request), (ChannelDataPtr)&reply, sizeof(reply)),
        success(size)
        {
            if (size != sizeof(reply) || reply.status != IO_OK)
            {
                return Error(IOStatus, bstr("Mount rejected"),
                             size == sizeof(reply) ? reply.status : IO_UNKNOWN_BUFFER);
            }
            return Success(IOStatus, IO_OK);
        },
        error(code, message)
        {
            return Error(IOStatus, message, code);
        }
    );
}
