#ifndef HUND_DEVICE_REGISTRY_H
#define HUND_DEVICE_REGISTRY_H

#include "../types/atomic_fixed_array.h"
#include "../types/atomic_record_array.h"
#include "../services/service_directory.h"

#define DEVICE_REGISTRY_VADDR 0x70800000ULL
#define DEVICE_REGISTRY_CAPACITY 512U
#define DEVICE_REGISTRY_VERSION 2ULL

typedef enum DeviceKind
{
    DEVICE_ABSENT,
    DEVICE_BLOCK,
    DEVICE_KERNEL_STREAM,
} DeviceKind;

typedef enum DeviceRegistryStatus
{
    DEVICE_REGISTRY_INVALID = -1,
    DEVICE_REGISTRY_BUSY = -2,
} DeviceRegistryStatus;

typedef struct DeviceRecord
{
    DeviceKind kind;
    uint64_t owner_pid;
    BString path;
    BString alias;
    BString service;
    uint64_t block_size;
    uint64_t block_count;
} DeviceRecord;

typedef DeviceRecord * DeviceRecordPtr;
typedef const DeviceRecord * ConstDeviceRecordPtr;
ResultDef(DeviceRecord);
OptionalDef(DeviceRecord);
typedef Optional(DeviceRecord) OptionalDeviceRecord;
ResultDef(OptionalDeviceRecord);

DeviceRecord DeviceRecord__new(DeviceKind kind, uint64_t owner_pid, ConstBStringPtr path, ConstBStringPtr alias,
                               uint64_t block_size, uint64_t block_count);

AtomicRecordArrayDefine(DeviceRecord, DEVICE_REGISTRY_CAPACITY, DeviceRecords);

typedef struct DeviceRegistry
{
    uint64_t version;
    DeviceRecords records;
} DeviceRegistry;

typedef DeviceRegistry * DeviceRegistryPtr;
typedef const DeviceRegistry * ConstDeviceRegistryPtr;

#define DEVICE_REGISTRY_SIZE (((sizeof(DeviceRegistry) + 4095ULL) / 4096ULL) * 4096ULL)
_Static_assert(HUND_SERVICE_DIRECTORY_VADDR + HUND_SERVICE_DIRECTORY_SIZE <= DEVICE_REGISTRY_VADDR,
               "Service directory overlaps device registry");
_Static_assert(DEVICE_REGISTRY_VADDR + DEVICE_REGISTRY_SIZE <= 0x71000000ULL,
               "Device registry overlaps managed payload pages");

ConstDeviceRegistryPtr DeviceRegistry__shared(void);
Result(DeviceRecord) DeviceRegistry__at(ConstDeviceRegistryPtr self, uint64_t index);
Result(OptionalDeviceRecord) DeviceRegistry__find(ConstDeviceRegistryPtr self, ConstBStringPtr path);
// Kernel writer only; callers must serialize writers. Readers never write the mapping.
Result(CollectionCode) DeviceRegistry__set(DeviceRegistryPtr self, uint64_t index, ConstDeviceRecordPtr record);

#endif
