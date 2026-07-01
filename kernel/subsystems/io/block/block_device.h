#ifndef __KERNEL_SUBSYSTEMS_IO_BLOCK_BLOCK_DEVICE_H
#define __KERNEL_SUBSYSTEMS_IO_BLOCK_BLOCK_DEVICE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "../types.h"
#include "../../../types/fixed_array.h"
#include "../../../types/optional.h"
#include "../../../types/result.h"
#include "../../../types/kstring_additional_types.h"
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

typedef struct BlockDevice BlockDevice;
typedef BlockDevice * BlockDevicePtr;
typedef struct Process Process;
typedef Process * ProcessPtr;
typedef struct Scheduler Scheduler;
typedef Scheduler * SchedulerPtr;
typedef struct KernelContext KernelContext;
typedef KernelContext * KernelContextPtr;
OptionalDef(KernelContextPtr);
OptionalDef(BlockRequestID);

OptionalDef(BlockDevicePtr);
FixedArrayDefine(KString, IO_MAX_DEVICES, BlockDevicePaths, kstring_equals);
ResultDef(BlockDevicePaths);

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
Result(BlockDeviceCode) BlockDeviceRegistry__register(
    KString path, ProcessPtr service_process, BlockCount block_count, SchedulerPtr scheduler
);
Optional(BlockDevicePtr) BlockDeviceRegistry__find(KString path);
BlockDevicePaths BlockDeviceRegistry__paths();
Result(ServiceCode) BlockDeviceService__get_request(
    ProcessPtr service_process, uint64_t request_id, ServiceBlockRequestPtr target
);
Result(ServiceCode) BlockDeviceService__complete(
    ProcessPtr service_process, ServiceBlockCompletionPtr completion
);

#endif
