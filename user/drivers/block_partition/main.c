#include "channel/block_client.h"
#include "channel/block_runtime.h"
#include "sys/service.h"

#define BLOCK_PARTITION_MBR_SIZE 512U
#define BLOCK_PARTITION_TABLE_OFFSET 446U
#define BLOCK_PARTITION_ENTRY_SIZE 16U
#define BLOCK_PARTITION_COUNT 4U

typedef struct BlockPartition
{
    BlockClient parent;
    BlockProtocolBlockNumber first_block;
    BlockProtocolBlockCount block_count;
} BlockPartition;

typedef BlockPartition * BlockPartitionPtr;

static BlockPartition BlockPartition__new(BlockClient parent, BlockProtocolBlockNumber first_block,
                                          BlockProtocolBlockCount block_count)
{
    return (BlockPartition){
        .parent = parent,
        .first_block = first_block,
        .block_count = block_count,
    };
}

typedef struct BlockPartitionEntry
{
    uint8_t type;
    BlockProtocolBlockNumber first_block;
    BlockProtocolBlockCount block_count;
} BlockPartitionEntry;

OptionalDef(BlockPartitionEntry);

static BlockPartitionEntry BlockPartitionEntry__new(uint8_t type, BlockProtocolBlockNumber first_block,
                                                    BlockProtocolBlockCount block_count)
{
    return (BlockPartitionEntry){
        .type = type,
        .first_block = first_block,
        .block_count = block_count,
    };
}

static uint32_t BlockPartition__read_u32(const uint8_t * bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) | ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
}

static Optional(BlockPartitionEntry) BlockPartition__entry(const uint8_t * mbr, size_t size, uint64_t partition_index,
                                                           BlockProtocolBlockCount parent_block_count)
{
    if (size != BLOCK_PARTITION_MBR_SIZE || partition_index == 0 || partition_index > BLOCK_PARTITION_COUNT ||
        mbr[510] != 0x55 || mbr[511] != 0xaa)
    {
        return None(BlockPartitionEntry);
    }

    size_t offset = BLOCK_PARTITION_TABLE_OFFSET + (partition_index - 1U) * BLOCK_PARTITION_ENTRY_SIZE;
    BlockPartitionEntry entry = BlockPartitionEntry__new(mbr[offset + 4U], BlockPartition__read_u32(mbr + offset + 8U),
                                                         BlockPartition__read_u32(mbr + offset + 12U));
    if (entry.type == 0 || entry.block_count == 0 || entry.first_block >= parent_block_count ||
        entry.block_count > parent_block_count - entry.first_block)
    {
        return None(BlockPartitionEntry);
    }
    return Some(BlockPartitionEntry, entry);
}

static BlockProtocolStatus BlockPartition__transfer(BlockServerContext context, BlockProtocolOperation operation,
                                                    BlockProtocolBlockNumber block, BlockProtocolBlockCount count,
                                                    ConstBufferDescriptorPtr buffer)
{
    BlockPartitionPtr self = context;
    if (count == 0 || block >= self->block_count || count > self->block_count - block ||
        count > buffer->capacity / BLOCK_CHANNEL_SECTOR_SIZE ||
        (operation != BLOCK_PROTOCOL_READ && operation != BLOCK_PROTOCOL_WRITE))
    {
        return BLOCK_PROTOCOL_INVALID_ARGUMENT;
    }

    Result(BlockProtocolBlockCount) transferred =
        operation == BLOCK_PROTOCOL_READ ? BlockClient__read(&self->parent, self->first_block + block, count,
                                                             buffer->address, count * BLOCK_CHANNEL_SECTOR_SIZE)
                                         : BlockClient__write(&self->parent, self->first_block + block, count,
                                                              buffer->address, count * BLOCK_CHANNEL_SECTOR_SIZE);
    match_result(
        transferred,
        success(blocks)
        {
            return blocks == count ? BLOCK_PROTOCOL_OK : BLOCK_PROTOCOL_IO_ERROR;
        },
        error(code, message)
        {
            return BLOCK_PROTOCOL_IO_ERROR;
        }
    );
    return BLOCK_PROTOCOL_IO_ERROR;
}

int main(int argc, char ** argv)
{
    if (argc != 3 || !service_ok(service_become_service()))
    {
        return 1;
    }

    BString parent_path = BString__new(argv[1]);
    BString partition_argument = BString__new(argv[2]);
    uint64_t partition_index = 0;
    match_optional(
        BString__parse_uint64(&partition_argument),
        some(value)
        {
            partition_index = value;
        },
        none()
        {
            return 1;
        }
    );

    BlockClient parent;
    match_result(
        BlockClient__open(&parent_path),
        success(client)
        {
            parent = client;
        },
        error(code, message)
        {
            return 1;
        }
    );

    uint8_t mbr[BLOCK_PARTITION_MBR_SIZE];
    match_result(
        BlockClient__read(&parent, 0, 1, (Address)mbr, sizeof(mbr)),
        success_do_nothing(),
        error(code, message)
        {
            BlockClient__close(&parent);
            return 1;
        }
    );

    BlockPartitionEntry entry;
    match_optional(
        BlockPartition__entry(mbr, sizeof(mbr), partition_index, parent.block_count),
        some(value)
        {
            entry = value;
        },
        none()
        {
            BlockClient__close(&parent);
            return 1;
        }
    );

    BlockPartition partition = BlockPartition__new(parent, entry.first_block, entry.block_count);
    BString path = BString__sprintf(&bstr("%Bp%llu"), &parent_path, partition_index);
    if (!service_ok(service_register_block_device(&path, BLOCK_CHANNEL_SECTOR_SIZE, partition.block_count)))
    {
        BlockClient__close(&partition.parent);
        return 1;
    }

    BlockServer server;
    if (BlockServer__listen_into(&server, &path, partition.block_count, &partition, BlockPartition__transfer) !=
        CHANNEL_OK)
    {
        BlockClient__close(&partition.parent);
        return 1;
    }
    if (!service_ok(service_publish_ready(&path)))
    {
        BlockClient__close(&partition.parent);
        return 1;
    }
    return BlockServer__run(&server);
}
