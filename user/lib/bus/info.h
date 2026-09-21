#ifndef HUND_BUS_INFO_H
#define HUND_BUS_INFO_H

#include "io/server.h"

#define BUS_INFO_SERVICE "hundebus"
#define BUS_INFO_MAX_DEVICES 256

typedef uint64_t BusDeviceIndex;
typedef uint64_t BusDeviceCount;

typedef struct BusPciDevice
{
    uint64_t bus;
    uint64_t device;
    uint64_t function;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t header_type;
} BusPciDevice;
typedef BusPciDevice * BusPciDevicePtr;
typedef const BusPciDevice * ConstBusPciDevicePtr;
OptionalDef(BusPciDevice);
bool BusPciDevice__equals(void * first, void * second);
FixedArrayDefine(BusPciDevice, BUS_INFO_MAX_DEVICES, BusPciDevices, BusPciDevice__equals);
typedef BusPciDevices * BusPciDevicesPtr;

// One record per page; count is zero at the end of the inventory.
typedef struct BusInfoPage
{
    BusDeviceCount total;
    BusDeviceCount count;
    BusPciDevice device;
} BusInfoPage;
ResultDef(BusInfoPage);

IORequest BusInfo__request(BusDeviceIndex index);
Result(BusInfoPage) BusInfo__read(ChannelPtr channel, BusDeviceIndex index);
Optional(IOReply)
    BusInfo__execute(IOServiceContext context, QueueConnectionID session, uint64_t pid, IORequestPtr request);
bool BusInfo__disconnect(IOServiceContext context, QueueConnectionID session, uint64_t pid);

#endif
