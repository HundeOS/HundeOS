#include "info.h"

FixedArrayBodyDefine(BusPciDevices);
_Static_assert(sizeof(BusInfoPage) <= IO_PROTOCOL_DATA_SIZE, "Bus page exceeds IO reply data");

bool BusPciDevice__equals(void * first, void * second)
{
    ConstBusPciDevicePtr left = first;
    ConstBusPciDevicePtr right = second;
    return left->bus == right->bus && left->device == right->device && left->function == right->function;
}

IORequest BusInfo__request(BusDeviceIndex index)
{
    IORequest request = IORequest__new(IO_REQUEST_READ, 0);
    request.offset = index;
    request.count = sizeof(BusInfoPage);
    return request;
}

static BusInfoPage BusInfoPage__new(BusDeviceCount total)
{
    return (BusInfoPage){.total = total};
}

Optional(IOReply)
    BusInfo__execute(IOServiceContext context, QueueConnectionID session, uint64_t pid, IORequestPtr request)
{
    BusPciDevicesPtr devices = context;
    if (request->operation != IO_REQUEST_READ || request->handle != 0 || request->flags != 0 ||
        request->count != sizeof(BusInfoPage))
    {
        return Some(IOReply, IOReply__new(IO_OPERATION_NOT_SUPPORTED, 0));
    }
    BusInfoPage page = BusInfoPage__new(BusPciDevices__len(devices));
    if (request->offset > page.total)
    {
        return Some(IOReply, IOReply__new(IO_INVALID_PATH, 0));
    }
    if (request->offset < page.total)
    {
        match_optional(
            BusPciDevices__at(devices, (ArrayIndex)request->offset),
            some(device)
            {
                page.device = device;
                page.count = 1;
            },
            none()
            {
                return Some(IOReply, IOReply__new(IO_PANIC, 0));
            }
        );
    }
    IOReply reply = IOReply__new(IO_OK, sizeof(page));
    __builtin_memcpy(reply.data, &page, sizeof(page));
    return Some(IOReply, reply);
}

bool BusInfo__disconnect(IOServiceContext context, QueueConnectionID session, uint64_t pid)
{
    return true;
}

Result(BusInfoPage) BusInfo__read(ChannelPtr channel, BusDeviceIndex index)
{
    IORequest request = BusInfo__request(index);
    IOReply reply;
    match_result(
        Channel__submit(channel, (ConstChannelDataPtr)&request, sizeof(request), (ChannelDataPtr)&reply, sizeof(reply)),
        success(size)
        {
            if (size != sizeof(reply))
            {
                return Error(BusInfoPage, bstr("Invalid bus reply size"), CHANNEL_INVALID_REPLY);
            }
        },
        error(code, message)
        {
            return Error(BusInfoPage, message, code);
        }
    );
    if (reply.status != IO_OK)
    {
        return Error(BusInfoPage, bstr("Bus query failed"), reply.status);
    }
    if (reply.value != sizeof(BusInfoPage))
    {
        return Error(BusInfoPage, bstr("Invalid bus page size"), CHANNEL_INVALID_REPLY);
    }
    BusInfoPage page;
    __builtin_memcpy(&page, reply.data, sizeof(page));
    if (page.total > BUS_INFO_MAX_DEVICES || index > page.total || page.count != (index < page.total ? 1 : 0))
    {
        return Error(BusInfoPage, bstr("Invalid bus page"), CHANNEL_INVALID_REPLY);
    }
    return Success(BusInfoPage, page);
}
