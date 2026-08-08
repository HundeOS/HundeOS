#include "memory_block_device.h"

LinkedListBodyDefine(MemoryDeviceBlocks);

static BString invalid_argument = BSTRING_NEW("Invalid memory block device argument");
static BString allocation_failed = BSTRING_NEW("Memory block device allocation failed");

bool MemoryDeviceBlock__equals(MemoryDeviceBlockPtr self, MemoryDeviceBlockPtr other)
{
    return self->block == other->block;
}

MemoryBlockDevice MemoryBlockDevice__new()
{
    return (MemoryBlockDevice) {
        .blocks = MemoryDeviceBlocks__new(),
        .lock = MUTEX_INIT,
    };
}

BlockDevice MemoryBlockDevice__as_block_device(MemoryBlockDevicePtr self)
{
    return BlockDevice__new(self, MemoryBlockDevice__read, MemoryBlockDevice__write);
}

static bool MemoryDeviceBlock__matches_number(MemoryDeviceBlockPtr block, void * context)
{
    BlockNumber number = *(BlockNumber *) context;
    return block->block == number;
}

static void MemoryBlockDevice__zero(DataPtr target)
{
    for (size_t i = 0; i < BLOCK_DEVICE_BLOCK_SIZE; i++)
    {
        target[i] = 0;
    }
}

static void MemoryBlockDevice__copy(DataPtr target, DataPtr source)
{
    for (size_t i = 0; i < BLOCK_DEVICE_BLOCK_SIZE; i++)
    {
        target[i] = source[i];
    }
}

static Result(MemoryDeviceBlockPtr) MemoryBlockDevice__find_or_create_block(
    MemoryBlockDevicePtr self, BlockNumber block
)
{
    LLFindResultMatch(MemoryDeviceBlocks, MemoryDeviceBlocks__find(&self->blocks, MemoryDeviceBlock__matches_number, &block))
        IsSome(found, return Success(MemoryDeviceBlockPtr, found.value))
    End

    KernelAddress address = Kernel__alloc(BLOCK_DEVICE_BLOCK_SIZE);
    if (!address.valid)
    {
        return Error(MemoryDeviceBlockPtr, allocation_failed, BLOCK_DEVICE_IO_ERROR);
    }

    MemoryBlockDevice__zero(v_ptr(address, Data));

    ResultMatch(CollectionCode, MemoryDeviceBlocks__push(&self->blocks, ((MemoryDeviceBlock) {
        .block = block,
        .address = address,
    })))
        IsSuccess(_, {
            LLFindResultMatch(
                MemoryDeviceBlocks,
                MemoryDeviceBlocks__find(&self->blocks, MemoryDeviceBlock__matches_number, &block)
            )
                IsSome(found, return Success(MemoryDeviceBlockPtr, found.value))
            End
        })
        IsError(code, message, return Error(MemoryDeviceBlockPtr, message, BLOCK_DEVICE_IO_ERROR))
    End

    return Error(MemoryDeviceBlockPtr, allocation_failed, BLOCK_DEVICE_IO_ERROR);
}

Result(BlockCount) MemoryBlockDevice__read(
    BlockDriverContextPtr context, BlockNumber block, BlockCount count, DataPtr target
)
{
    MemoryBlockDevicePtr self = (MemoryBlockDevicePtr) context;
    if (self == NULL || target == NULL || count == 0)
    {
        return Error(BlockCount, invalid_argument, BLOCK_DEVICE_INVALID_ARGUMENT);
    }

    CriticalSection(self->lock,
    {
        for (BlockCount i = 0; i < count; i++)
        {
            DataPtr block_target = target + (i * BLOCK_DEVICE_BLOCK_SIZE);
            BlockNumber current_block = block + i;

            LLFindResultMatch(
                MemoryDeviceBlocks,
                MemoryDeviceBlocks__find(&self->blocks, MemoryDeviceBlock__matches_number, &current_block)
            )
                IsSome(found, MemoryBlockDevice__copy(block_target, v_ptr(found.value->address, Data)))
                IsNone(MemoryBlockDevice__zero(block_target))
            End
        }

        _cs_return Success(BlockCount, count);
    });
}

Result(BlockCount) MemoryBlockDevice__write(
    BlockDriverContextPtr context, BlockNumber block, BlockCount count, DataPtr source
)
{
    MemoryBlockDevicePtr self = (MemoryBlockDevicePtr) context;
    if (self == NULL || source == NULL || count == 0)
    {
        return Error(BlockCount, invalid_argument, BLOCK_DEVICE_INVALID_ARGUMENT);
    }

    CriticalSection(self->lock,
    {
        for (BlockCount i = 0; i < count; i++)
        {
            ResultMatch(MemoryDeviceBlockPtr, MemoryBlockDevice__find_or_create_block(self, block + i))
                IsSuccess(memory_block, {
                    DataPtr block_source = source + (i * BLOCK_DEVICE_BLOCK_SIZE);
                    MemoryBlockDevice__copy(v_ptr(memory_block->address, Data), block_source);
                })
                IsError(code, message, _cs_return Error(BlockCount, message, code))
            End
        }

        _cs_return Success(BlockCount, count);
    });
}
