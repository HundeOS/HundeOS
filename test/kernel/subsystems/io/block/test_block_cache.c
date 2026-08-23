#include "unity.h"

#include "../../../../../kernel/subsystems/io/block/block_cache.h"
#include "../../../../../kernel/subsystems/io/block/memory_block_device.h"
#include "../../../stubs/panic.h"

#define FAKE_BLOCK_COUNT 8

typedef struct FakeBlockDriver
{
    Data blocks[FAKE_BLOCK_COUNT][BLOCK_DEVICE_BLOCK_SIZE];
    BlockNumber last_read_block;
    BlockCount read_count;
}
FakeBlockDriver;

static FakeBlockDriver fake;
static BlockDevice device;
static BlockCache cache;
static MemoryBlockDevice memory_device;
static BlockDevice memory_block_device;

static Result(BlockCount) fake_read(
    BlockDriverContextPtr context, BlockNumber block, BlockCount count, DataPtr target
)
{
    FakeBlockDriver * self = (FakeBlockDriver *) context;
    self->last_read_block = block;
    self->read_count++;

    if (block >= FAKE_BLOCK_COUNT || count != 1)
    {
        return Error(BlockCount, bstring("invalid block"), BLOCK_DEVICE_IO_ERROR);
    }

    for (size_t i = 0; i < BLOCK_DEVICE_BLOCK_SIZE; i++)
    {
        target[i] = self->blocks[block][i];
    }

    return Success(BlockCount, count);
}

static Result(BlockCount) fake_write(
    BlockDriverContextPtr context, BlockNumber block, BlockCount count, DataPtr source
)
{
    FakeBlockDriver * self = (FakeBlockDriver *) context;

    if (block >= FAKE_BLOCK_COUNT || count != 1)
    {
        return Error(BlockCount, bstring("invalid block"), BLOCK_DEVICE_IO_ERROR);
    }

    for (size_t i = 0; i < BLOCK_DEVICE_BLOCK_SIZE; i++)
    {
        self->blocks[block][i] = source[i];
    }

    return Success(BlockCount, count);
}

void setUp(void)
{
    clear_panic_state();
    fake = (FakeBlockDriver) {
        .last_read_block = 0,
        .read_count = 0,
    };

    for (size_t block = 0; block < FAKE_BLOCK_COUNT; block++)
    {
        for (size_t offset = 0; offset < BLOCK_DEVICE_BLOCK_SIZE; offset++)
        {
            fake.blocks[block][offset] = (Data) (block + offset);
        }
    }

    device = BlockDevice__new(&fake, fake_read, fake_write);
    cache = BlockCache__new(&device);
    memory_device = MemoryBlockDevice__new();
    memory_block_device = MemoryBlockDevice__as_block_device(&memory_device);
}

void tearDown(void)
{
    TEST_ASSERT_FALSE(panic_occurred());
}

void test_block_cache_reads_block_from_device(void)
{
    Data target[BLOCK_DEVICE_BLOCK_SIZE] = { 0 };

    match_result(BlockDeviceCode, BlockCache__read(&cache, 3, target), success(code) { TEST_ASSERT_EQUAL(BLOCK_DEVICE_OK, code); }, error(code, message) { TEST_FAIL(); })

    TEST_ASSERT_EQUAL(3, fake.last_read_block);
    TEST_ASSERT_EQUAL(1, fake.read_count);
    TEST_ASSERT_EQUAL(fake.blocks[3][0], target[0]);
    TEST_ASSERT_EQUAL(fake.blocks[3][17], target[17]);
}

void test_block_cache_serves_second_read_from_cache(void)
{
    Data first[BLOCK_DEVICE_BLOCK_SIZE] = { 0 };
    Data second[BLOCK_DEVICE_BLOCK_SIZE] = { 0 };

    match_result(BlockDeviceCode, BlockCache__read(&cache, 2, first), success(_) {}, error(code, message) { TEST_FAIL(); })

    fake.blocks[2][0] = 99;

    match_result(BlockDeviceCode, BlockCache__read(&cache, 2, second), success(_) {}, error(code, message) { TEST_FAIL(); })

    TEST_ASSERT_EQUAL(1, fake.read_count);
    TEST_ASSERT_EQUAL(first[0], second[0]);
    TEST_ASSERT_NOT_EQUAL(fake.blocks[2][0], second[0]);
}

void test_block_cache_rejects_invalid_arguments(void)
{
    Data target[BLOCK_DEVICE_BLOCK_SIZE] = { 0 };

    match_result(BlockDeviceCode, BlockCache__read(NULL, 0, target), success(code) { TEST_FAIL(); }, error(code, message) { TEST_ASSERT_EQUAL(BLOCK_DEVICE_INVALID_ARGUMENT, code); })

    match_result(BlockDeviceCode, BlockCache__read(&cache, 0, NULL), success(code) { TEST_FAIL(); }, error(code, message) { TEST_ASSERT_EQUAL(BLOCK_DEVICE_INVALID_ARGUMENT, code); })
}

void test_memory_block_device_reads_unwritten_blocks_as_zero(void)
{
    Data target[BLOCK_DEVICE_BLOCK_SIZE] = { 7 };

    match_result(BlockCount, BlockDevice__read(&memory_block_device, 4, 1, target), success(count) { TEST_ASSERT_EQUAL(1, count); }, error(code, message) { TEST_FAIL(); })

    for (size_t i = 0; i < BLOCK_DEVICE_BLOCK_SIZE; i++)
    {
        TEST_ASSERT_EQUAL(0, target[i]);
    }
}

void test_memory_block_device_writes_and_reads_single_block(void)
{
    Data source[BLOCK_DEVICE_BLOCK_SIZE] = { 0 };
    Data target[BLOCK_DEVICE_BLOCK_SIZE] = { 0 };

    for (size_t i = 0; i < BLOCK_DEVICE_BLOCK_SIZE; i++)
    {
        source[i] = (Data) (255 - (i % 256));
    }

    match_result(BlockCount, BlockDevice__write(&memory_block_device, 2, 1, source), success(count) { TEST_ASSERT_EQUAL(1, count); }, error(code, message) { TEST_FAIL(); })

    match_result(BlockCount, BlockDevice__read(&memory_block_device, 2, 1, target), success(count) { TEST_ASSERT_EQUAL(1, count); }, error(code, message) { TEST_FAIL(); })

    TEST_ASSERT_EQUAL_MEMORY(source, target, BLOCK_DEVICE_BLOCK_SIZE);
    TEST_ASSERT_EQUAL(1, MemoryDeviceBlocks__len(&memory_device.blocks));
}

void test_memory_block_device_writes_and_reads_multiple_blocks(void)
{
    Data source[BLOCK_DEVICE_BLOCK_SIZE * 2] = { 0 };
    Data target[BLOCK_DEVICE_BLOCK_SIZE * 2] = { 0 };

    for (size_t i = 0; i < BLOCK_DEVICE_BLOCK_SIZE * 2; i++)
    {
        source[i] = (Data) (i % 251);
    }

    match_result(BlockCount, BlockDevice__write(&memory_block_device, 5, 2, source), success(count) { TEST_ASSERT_EQUAL(2, count); }, error(code, message) { TEST_FAIL(); })

    match_result(BlockCount, BlockDevice__read(&memory_block_device, 5, 2, target), success(count) { TEST_ASSERT_EQUAL(2, count); }, error(code, message) { TEST_FAIL(); })

    TEST_ASSERT_EQUAL_MEMORY(source, target, BLOCK_DEVICE_BLOCK_SIZE * 2);
    TEST_ASSERT_EQUAL(2, MemoryDeviceBlocks__len(&memory_device.blocks));
}

void test_memory_block_device_rewrites_existing_block_without_allocating_another(void)
{
    Data source[BLOCK_DEVICE_BLOCK_SIZE] = { 1 };
    Data replacement[BLOCK_DEVICE_BLOCK_SIZE] = { 2 };
    Data target[BLOCK_DEVICE_BLOCK_SIZE] = { 0 };

    match_result(BlockCount, BlockDevice__write(&memory_block_device, 1, 1, source), success(_) {}, error(code, message) { TEST_FAIL(); })
    match_result(BlockCount, BlockDevice__write(&memory_block_device, 1, 1, replacement), success(_) {}, error(code, message) { TEST_FAIL(); })
    match_result(BlockCount, BlockDevice__read(&memory_block_device, 1, 1, target), success(_) {}, error(code, message) { TEST_FAIL(); })

    TEST_ASSERT_EQUAL_MEMORY(replacement, target, BLOCK_DEVICE_BLOCK_SIZE);
    TEST_ASSERT_EQUAL(1, MemoryDeviceBlocks__len(&memory_device.blocks));
}
