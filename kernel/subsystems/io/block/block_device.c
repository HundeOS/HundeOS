#include "block_device.h"

static KString invalid_argument = KSTRING_NEW("Invalid block device argument");

BlockDevice BlockDevice__new(BlockDriverContextPtr context, BlockDeviceRead read, BlockDeviceWrite write)
{
    return (BlockDevice) {
        .context = context,
        .read = read,
        .write = write,
        .block_size = BLOCK_DEVICE_BLOCK_SIZE,
    };
}

Result(BlockCount) BlockDevice__read(BlockDevicePtr self, BlockNumber block, BlockCount count, DataPtr target)
{
    if (self == NULL || self->read == NULL || target == NULL || count == 0)
    {
        return Error(BlockCount, invalid_argument, BLOCK_DEVICE_INVALID_ARGUMENT);
    }

    return self->read(self->context, block, count, target);
}

Result(BlockCount) BlockDevice__write(BlockDevicePtr self, BlockNumber block, BlockCount count, DataPtr source)
{
    if (self == NULL || self->write == NULL || source == NULL || count == 0)
    {
        return Error(BlockCount, invalid_argument, BLOCK_DEVICE_INVALID_ARGUMENT);
    }

    return self->write(self->context, block, count, source);
}
