#ifndef __KERNEL_SUBSYSTEMS_IO_BLOCK_BLOCK_DEVICE_H
#define __KERNEL_SUBSYSTEMS_IO_BLOCK_BLOCK_DEVICE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "../types.h"
#include "../../../types/fixed_array.h"
#include "../../../types/optional.h"
#include "../../../types/result.h"
#include "../../../types/bstring_additional_types.h"
#include "../../../types/types.h"
#include "../../../service/service.h"
#include "../../../scheduler/kernel_context.h"

#define BLOCK_DEVICE_BLOCK_SIZE 512ULL

typedef uint64_t BlockNumber;
typedef uint64_t BlockCount;
typedef uint64_t BlockRequestID;
typedef void * BlockDriverContextPtr;

typedef enum BlockDeviceCode
{
    BLOCK_DEVICE_OK = 0,
    BLOCK_DEVICE_INVALID_ARGUMENT = -1,
    BLOCK_DEVICE_IO_ERROR = -2,
    BLOCK_DEVICE_ALREADY_REGISTERED = -3,
    BLOCK_DEVICE_REGISTRY_FULL = -4,
    BLOCK_DEVICE_REQUEST_PENDING = -5,
} BlockDeviceCode;

ResultDef(BlockDeviceCode);
ResultDef(BlockCount);
ResultDef(ServiceBlockRequest);

typedef struct BlockDevice BlockDevice;
typedef BlockDevice * BlockDevicePtr;
typedef struct Process Process;
typedef Process * ProcessPtr;
typedef struct MessageBroker MessageBroker;
typedef MessageBroker * MessageBrokerPtr;
OptionalDef(BlockRequestID);

OptionalDef(BlockDevicePtr);
FixedArrayDefine(BString, IO_MAX_DEVICES, BlockDevicePaths, bstring_equals);
ResultDef(BlockDevicePaths);

typedef struct BlockDeviceMetadata
{
    BString alias;
    BString path;
    size_t block_size;
    BlockCount block_count;
    bool userspace_service;
}
BlockDeviceMetadata;

typedef BlockDeviceMetadata * BlockDeviceMetadataPtr;

OptionalDef(BlockDeviceMetadata);
bool BlockDeviceMetadata__equals(BlockDeviceMetadataPtr self, BlockDeviceMetadataPtr other);
BlockDeviceMetadata BlockDeviceMetadata__new(
    BString alias,
    BString path,
    size_t block_size,
    BlockCount block_count,
    bool userspace_service
);
FixedArrayDefine(
    BlockDeviceMetadata,
    IO_MAX_DEVICES,
    BlockDeviceMetadataEntries,
    (EqualFunction) BlockDeviceMetadata__equals
);
ResultDef(BlockDeviceMetadataEntries);

typedef struct BlockIOContext BlockIOContext;
typedef BlockIOContext * BlockIOContextPtr;
typedef Result(Anything) (* BlockIOContinuation)(BlockIOContextPtr context);

OptionalDef(BlockIOContinuation);

typedef struct BlockIOContext
{
    KernelContext kernel_context;
    BlockNumber block;
    BlockCount block_count;
    DataPtr buffer;
    size_t buffer_size;
    BlockCount completed_blocks;
    BlockDeviceCode status;
    Anything owner_context;
    Optional(BlockRequestID) request_id;
}
BlockIOContext;

static inline BlockIOContext BlockIOContext__new(KernelContext kernel_context, Anything owner_context)
{
    return (BlockIOContext) {
        .kernel_context = kernel_context,
        .owner_context = owner_context,
        .request_id = None(BlockRequestID),
    };
}

typedef Result(BlockCount) (* BlockDeviceRead)(
    BlockDriverContextPtr context, BlockNumber block, BlockCount count, DataPtr target
);
typedef Result(BlockCount) (* BlockDeviceWrite)(
    BlockDriverContextPtr context, BlockNumber block, BlockCount count, DataPtr source
);

typedef struct BlockDevice
{
    BlockDriverContextPtr context;
    BlockDeviceRead read;
    BlockDeviceWrite write;
    size_t block_size;
    bool userspace_service;
}
BlockDevice;

BlockDevice BlockDevice__new(BlockDriverContextPtr context, BlockDeviceRead read, BlockDeviceWrite write);
Result(BlockCount) BlockDevice__read(BlockDevicePtr self, BlockNumber block, BlockCount count, DataPtr target);
Result(BlockCount) BlockDevice__write(BlockDevicePtr self, BlockNumber block, BlockCount count, DataPtr source);
Result(Anything) BlockDevice__read_async(
    BlockDevicePtr self, BlockNumber block, BlockCount count,
    BlockIOContextPtr context, BlockIOContinuation continuation
);
Result(Anything) BlockDevice__write_async(
    BlockDevicePtr self, BlockNumber block, BlockCount count,
    DataPtr source, BlockIOContextPtr context, BlockIOContinuation continuation
);
void BlockDevice__set_current_context(KernelContextPtr context);
void BlockDevice__clear_current_context();
Optional(KernelContextPtr) BlockDevice__current_context();
void BlockDeviceRegistry__reset(void);
Result(BlockDeviceCode) BlockDeviceRegistry__register(
    BString path, ProcessPtr service_process, BlockCount block_count, MessageBrokerPtr message_broker
);
Result(BlockCount) BlockDeviceRegistry__read_from_process(
    ProcessPtr requester,
    BString path,
    BlockNumber block,
    BlockCount count,
    DataPtr requester_buffer,
    size_t buffer_size,
    MessageBrokerPtr message_broker
);
Result(BlockCount) BlockDeviceRegistry__write_from_process(
    ProcessPtr requester,
    BString path,
    BlockNumber block,
    BlockCount count,
    DataPtr requester_buffer,
    size_t buffer_size,
    MessageBrokerPtr message_broker
);
Result(BlockDeviceCode) BlockDeviceRegistry__dispose_process_resources(ProcessPtr process);
Optional(BlockDevicePtr) BlockDeviceRegistry__find(BString path);
BlockDevicePaths BlockDeviceRegistry__paths();
BlockDeviceMetadataEntries BlockDeviceRegistry__metadata_entries();
Optional(BlockDeviceMetadata) BlockDeviceRegistry__metadata(BString alias);
Result(ServiceBlockRequest) BlockDeviceRegistry__get_request(ProcessPtr service_process, uint64_t request_id);
Result(ServiceCode) BlockDeviceService__complete(
    ProcessPtr service_process, ServiceBlockCompletionPtr completion
);

#endif
