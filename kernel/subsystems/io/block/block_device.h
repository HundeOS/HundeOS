#ifndef __KERNEL_SUBSYSTEMS_IO_BLOCK_BLOCK_DEVICE_H
#define __KERNEL_SUBSYSTEMS_IO_BLOCK_BLOCK_DEVICE_H

#include <stddef.h>
#include <stdint.h>

#include "../types.h"
#include "../../../types/result.h"
#include "../../../types/kstring.h"

#define BLOCK_DEVICE_BLOCK_SIZE 512ULL

typedef uint64_t BlockNumber;
typedef uint64_t BlockCount;
typedef void * BlockDriverContextPtr;

typedef enum BlockDeviceCode
{
    BLOCK_DEVICE_OK = 0,
    BLOCK_DEVICE_INVALID_ARGUMENT = -1,
    BLOCK_DEVICE_IO_ERROR = -2,
} BlockDeviceCode;

ResultDef(BlockDeviceCode);
ResultDef(BlockCount);

typedef struct BlockDevice BlockDevice;
typedef BlockDevice * BlockDevicePtr;

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
}
BlockDevice;

BlockDevice BlockDevice__new(BlockDriverContextPtr context, BlockDeviceRead read, BlockDeviceWrite write);
Result(BlockCount) BlockDevice__read(BlockDevicePtr self, BlockNumber block, BlockCount count, DataPtr target);
Result(BlockCount) BlockDevice__write(BlockDevicePtr self, BlockNumber block, BlockCount count, DataPtr source);

#endif
