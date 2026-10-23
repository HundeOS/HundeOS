#include "mount_client.h"
#include "memory.h"
#include "protocol.h"
#include "sys/service.h"

Result(IOStatus) MountClient__start_hosted(MountConfig config)
{
    ServiceCode ready = service_wait_ready(&bstr(IO_HOST_MOUNT_SERVICE));
    if (!service_ok(ready))
    {
        return Error(IOStatus, bstr("I/O host mount control is not ready"), ready);
    }
    match_result(
        Channel__open(ServiceDirectory__shared(), &bstr(IO_HOST_MOUNT_SERVICE)),
        success(channel)
        {
            Result(IOStatus) result = MountClient__add(&channel, config);
            Channel__close(&channel);
            return result;
        },
        error(code, message)
        {
            return Error(IOStatus, message, code);
        }
    );
    return Error(IOStatus, bstr("Unknown error"), RESULT_UNKNOWN_ERROR_CODE);
}

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
    Mem__memcpy(request.data, BString__cstr(&line), request.count);
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
