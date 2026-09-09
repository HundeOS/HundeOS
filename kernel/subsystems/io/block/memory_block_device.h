#ifndef __KERNEL_SUBSYSTEMS_IO_BLOCK_MEMORY_BLOCK_DEVICE_H
#define __KERNEL_SUBSYSTEMS_IO_BLOCK_MEMORY_BLOCK_DEVICE_H

#include "block_device.h"
#include "../../../address.h"
#include "../../../mutex.h"
#include "../../../types/linked_list.h"

typedef struct MemoryDeviceBlock
{
    BlockNumber block;
    KernelAddress address;
} MemoryDeviceBlock;

typedef MemoryDeviceBlock * MemoryDeviceBlockPtr;

OptionalDef(MemoryDeviceBlock);
OptionalDef(MemoryDeviceBlockPtr);
ResultDef(MemoryDeviceBlockPtr);

bool MemoryDeviceBlock__equals(MemoryDeviceBlockPtr self, MemoryDeviceBlockPtr other);
LinkedListDefine(MemoryDeviceBlock, MemoryDeviceBlocks, (EqualFunction)MemoryDeviceBlock__equals);

typedef struct MemoryBlockDevice
{
    MemoryDeviceBlocks blocks;
    mutex_t lock;
} MemoryBlockDevice;

typedef MemoryBlockDevice * MemoryBlockDevicePtr;

MemoryBlockDevice MemoryBlockDevice__new();
BlockDevice MemoryBlockDevice__as_block_device(MemoryBlockDevicePtr self);
Result(BlockCount)
    MemoryBlockDevice__read(BlockDriverContextPtr context, BlockNumber block, BlockCount count, DataPtr target);
Result(BlockCount)
    MemoryBlockDevice__write(BlockDriverContextPtr context, BlockNumber block, BlockCount count, DataPtr source);

#endif
