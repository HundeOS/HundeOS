#include <string.h>
#include <stdlib.h>

#include "unity.h"
#include "ipc/io_diagnostics.h"

#include "../../../user/lib/channel/block_cache.c"

#define TEST_BLOCK_COUNT (BLOCK_CACHE_CAPACITY * 2U + 8U)

static uint8_t disk[TEST_BLOCK_COUNT][BLOCK_CHANNEL_SECTOR_SIZE];
static uint64_t read_calls;
static uint64_t write_calls;
static bool fail_reads;
static bool fail_writes;
static bool allow_reclaimable_allocations;
static uint64_t reclaimable_allocations;
static uint64_t reclaimable_frees;
static IODiagnostics diagnostics;
static BString io_error = bstr("fake block IO error");

Result(Anything) mem_alloc_reclaimable_pages_result(size_t size)
{
    if (!allow_reclaimable_allocations)
    {
        return Error(Anything, bstr("reclaimable allocation refused"), MEM_ALLOCATION_FAILED);
    }
    void * allocation = calloc(1, size);
    TEST_ASSERT_NOT_NULL(allocation);
    reclaimable_allocations++;
    return Success(Anything, (Anything)allocation);
}

MemStatus mem_free_pages(void * address)
{
    free(address);
    reclaimable_frees++;
    return MEM_OK;
}

Result(BlockProtocolBlockCount)
    BlockClient__read(BlockClientPtr self, BlockProtocolBlockNumber block, BlockProtocolBlockCount block_count,
                      Address buffer, size_t buffer_size)
{
    read_calls++;
    if (fail_reads)
    {
        return Error(BlockProtocolBlockCount, io_error, BLOCK_PROTOCOL_IO_ERROR);
    }
    TEST_ASSERT_LESS_OR_EQUAL_UINT64(TEST_BLOCK_COUNT, block + block_count);
    TEST_ASSERT_EQUAL_UINT64(block_count * BLOCK_CHANNEL_SECTOR_SIZE, buffer_size);
    memcpy((void *)buffer, disk[block], buffer_size);
    return Success(BlockProtocolBlockCount, block_count);
}

Result(BlockProtocolBlockCount)
    BlockClient__write(BlockClientPtr self, BlockProtocolBlockNumber block, BlockProtocolBlockCount block_count,
                       Address buffer, size_t buffer_size)
{
    write_calls++;
    if (fail_writes)
    {
        return Error(BlockProtocolBlockCount, io_error, BLOCK_PROTOCOL_IO_ERROR);
    }
    TEST_ASSERT_LESS_OR_EQUAL_UINT64(TEST_BLOCK_COUNT, block + block_count);
    TEST_ASSERT_EQUAL_UINT64(block_count * BLOCK_CHANNEL_SECTOR_SIZE, buffer_size);
    memcpy(disk[block], (void *)buffer, buffer_size);
    return Success(BlockProtocolBlockCount, block_count);
}

void setUp(void)
{
    for (uint64_t block = 0; block < TEST_BLOCK_COUNT; block++)
    {
        memset(disk[block], (int)(block + 1U), sizeof(disk[block]));
    }
    read_calls = 0;
    write_calls = 0;
    fail_reads = false;
    fail_writes = false;
    allow_reclaimable_allocations = false;
    reclaimable_allocations = 0;
    reclaimable_frees = 0;
    diagnostics = (IODiagnostics){0};
    IODiagnostics__attach(&diagnostics);
}

void test_block_cache_grows_with_reclaimable_page_backed_chunk(void)
{
    BlockCache cache = BlockCache__new();
    BlockClient client = {0};
    uint8_t data[BLOCK_CHANNEL_SECTOR_SIZE];
    allow_reclaimable_allocations = true;

    for (uint64_t block = 0; block <= BLOCK_CACHE_CAPACITY; block++)
    {
        TEST_ASSERT_EQUAL(1,
                          unwrap_result(BlockCache__read(&cache, &client, block, 1, (Address)data, sizeof(data)), 0));
    }

    TEST_ASSERT_EQUAL_UINT64(BLOCK_CACHE_CAPACITY * 2U, BlockCache__capacity(&cache));
    TEST_ASSERT_EQUAL_UINT64(1, reclaimable_allocations);
    uint64_t reads_before_lookup = read_calls;
    TEST_ASSERT_EQUAL(1, unwrap_result(BlockCache__read(&cache, &client, 1, 1, (Address)data, sizeof(data)), 0));
    TEST_ASSERT_EQUAL_UINT64(reads_before_lookup, read_calls);
    BlockCache__reclaim(&cache);
}

void test_block_cache_reclaim_releases_dynamic_chunks_and_keeps_base(void)
{
    BlockCache cache = BlockCache__new();
    BlockClient client = {0};
    uint8_t data[BLOCK_CHANNEL_SECTOR_SIZE];
    allow_reclaimable_allocations = true;

    for (uint64_t block = 0; block <= BLOCK_CACHE_CAPACITY; block++)
    {
        TEST_ASSERT_EQUAL(1,
                          unwrap_result(BlockCache__read(&cache, &client, block, 1, (Address)data, sizeof(data)), 0));
    }
    BlockCache__reclaim(&cache);

    TEST_ASSERT_EQUAL_UINT64(BLOCK_CACHE_CAPACITY, BlockCache__capacity(&cache));
    TEST_ASSERT_EQUAL_UINT64(1, reclaimable_frees);
    uint64_t reads_before_base_lookup = read_calls;
    TEST_ASSERT_EQUAL(1, unwrap_result(BlockCache__read(&cache, &client, 0, 1, (Address)data, sizeof(data)), 0));
    TEST_ASSERT_EQUAL_UINT64(reads_before_base_lookup, read_calls);
    TEST_ASSERT_EQUAL(
        1, unwrap_result(BlockCache__read(&cache, &client, BLOCK_CACHE_CAPACITY, 1, (Address)data, sizeof(data)), 0));
    TEST_ASSERT_EQUAL_UINT64(reads_before_base_lookup + 1U, read_calls);
}

void tearDown(void)
{
}

void test_block_cache_reuses_read_sectors(void)
{
    BlockCache cache = BlockCache__new();
    BlockClient client = {0};
    uint8_t first[2U * BLOCK_CHANNEL_SECTOR_SIZE];
    uint8_t second[2U * BLOCK_CHANNEL_SECTOR_SIZE];

    TEST_ASSERT_EQUAL(2, unwrap_result(BlockCache__read(&cache, &client, 3, 2, (Address)first, sizeof(first)), 0));
    TEST_ASSERT_EQUAL(2, unwrap_result(BlockCache__read(&cache, &client, 3, 2, (Address)second, sizeof(second)), 0));
    TEST_ASSERT_EQUAL_MEMORY(first, second, sizeof(first));
    TEST_ASSERT_EQUAL_UINT64(1, read_calls);
    IODiagnosticsSnapshot snapshot = IODiagnostics__snapshot(&diagnostics);
    TEST_ASSERT_EQUAL_UINT64(2, snapshot.block_cache_hits);
    TEST_ASSERT_EQUAL_UINT64(2, snapshot.block_cache_misses);
}

void test_block_cache_fetches_only_contiguous_miss_runs(void)
{
    BlockCache cache = BlockCache__new();
    BlockClient client = {0};
    uint8_t middle[2U * BLOCK_CHANNEL_SECTOR_SIZE];
    uint8_t all[4U * BLOCK_CHANNEL_SECTOR_SIZE];

    TEST_ASSERT_EQUAL(2, unwrap_result(BlockCache__read(&cache, &client, 2, 2, (Address)middle, sizeof(middle)), 0));
    TEST_ASSERT_EQUAL(4, unwrap_result(BlockCache__read(&cache, &client, 1, 4, (Address)all, sizeof(all)), 0));
    TEST_ASSERT_EQUAL_UINT64(3, read_calls);
    for (uint64_t index = 0; index < 4; index++)
    {
        TEST_ASSERT_EACH_EQUAL_UINT8(index + 2U, all + index * BLOCK_CHANNEL_SECTOR_SIZE, BLOCK_CHANNEL_SECTOR_SIZE);
    }
}

void test_block_cache_evicts_least_recently_used_sector(void)
{
    BlockCache cache = BlockCache__new();
    BlockClient client = {0};
    uint8_t data[BLOCK_CHANNEL_SECTOR_SIZE];

    for (uint64_t block = 0; block < BLOCK_CACHE_CAPACITY; block++)
    {
        TEST_ASSERT_EQUAL(1,
                          unwrap_result(BlockCache__read(&cache, &client, block, 1, (Address)data, sizeof(data)), 0));
    }
    TEST_ASSERT_EQUAL(1, unwrap_result(BlockCache__read(&cache, &client, 0, 1, (Address)data, sizeof(data)), 0));
    TEST_ASSERT_EQUAL(
        1, unwrap_result(BlockCache__read(&cache, &client, BLOCK_CACHE_CAPACITY, 1, (Address)data, sizeof(data)), 0));
    TEST_ASSERT_EQUAL(1, unwrap_result(BlockCache__read(&cache, &client, 1, 1, (Address)data, sizeof(data)), 0));
    TEST_ASSERT_EQUAL_UINT64(BLOCK_CACHE_CAPACITY + 2U, read_calls);
    IODiagnosticsSnapshot snapshot = IODiagnostics__snapshot(&diagnostics);
    TEST_ASSERT_EQUAL_UINT64(2, snapshot.block_cache_evictions);
}

void test_block_cache_preserves_frequent_sector_during_scan(void)
{
    BlockCache cache = BlockCache__new();
    BlockClient client = {0};
    uint8_t data[BLOCK_CHANNEL_SECTOR_SIZE];

    for (uint64_t block = 0; block < BLOCK_CACHE_CAPACITY; block++)
    {
        TEST_ASSERT_EQUAL(1,
                          unwrap_result(BlockCache__read(&cache, &client, block, 1, (Address)data, sizeof(data)), 0));
    }
    TEST_ASSERT_EQUAL(1, unwrap_result(BlockCache__read(&cache, &client, 0, 1, (Address)data, sizeof(data)), 0));
    for (uint64_t block = BLOCK_CACHE_CAPACITY; block < TEST_BLOCK_COUNT; block++)
    {
        TEST_ASSERT_EQUAL(1,
                          unwrap_result(BlockCache__read(&cache, &client, block, 1, (Address)data, sizeof(data)), 0));
    }

    uint64_t reads_before_hot_lookup = read_calls;
    TEST_ASSERT_EQUAL(1, unwrap_result(BlockCache__read(&cache, &client, 0, 1, (Address)data, sizeof(data)), 0));
    TEST_ASSERT_EQUAL_UINT64(reads_before_hot_lookup, read_calls);
}

void test_block_cache_write_through_updates_cached_sector(void)
{
    BlockCache cache = BlockCache__new();
    BlockClient client = {0};
    uint8_t data[BLOCK_CHANNEL_SECTOR_SIZE];
    uint8_t replacement[BLOCK_CHANNEL_SECTOR_SIZE];
    memset(replacement, 0xa5, sizeof(replacement));

    TEST_ASSERT_EQUAL(1, unwrap_result(BlockCache__read(&cache, &client, 4, 1, (Address)data, sizeof(data)), 0));
    TEST_ASSERT_EQUAL(
        1, unwrap_result(BlockCache__write(&cache, &client, 4, 1, (Address)replacement, sizeof(replacement)), 0));
    TEST_ASSERT_EQUAL(1, unwrap_result(BlockCache__read(&cache, &client, 4, 1, (Address)data, sizeof(data)), 0));
    TEST_ASSERT_EQUAL_MEMORY(replacement, data, sizeof(data));
    TEST_ASSERT_EQUAL_UINT64(1, read_calls);
    TEST_ASSERT_EQUAL_UINT64(1, write_calls);
}

void test_block_cache_does_not_admit_uncached_streaming_write(void)
{
    BlockCache cache = BlockCache__new();
    BlockClient client = {0};
    uint8_t replacement[BLOCK_CHANNEL_SECTOR_SIZE];
    uint8_t reread[BLOCK_CHANNEL_SECTOR_SIZE];
    memset(replacement, 0xa5, sizeof(replacement));

    TEST_ASSERT_EQUAL(
        1, unwrap_result(BlockCache__write(&cache, &client, 4, 1, (Address)replacement, sizeof(replacement)), 0));
    TEST_ASSERT_EQUAL(1, unwrap_result(BlockCache__read(&cache, &client, 4, 1, (Address)reread, sizeof(reread)), 0));
    TEST_ASSERT_EQUAL_MEMORY(replacement, reread, sizeof(replacement));
    TEST_ASSERT_EQUAL_UINT64(1, read_calls);
    TEST_ASSERT_EQUAL_UINT64(1, write_calls);
}

void test_block_cache_failed_write_preserves_cached_sector(void)
{
    BlockCache cache = BlockCache__new();
    BlockClient client = {0};
    uint8_t original[BLOCK_CHANNEL_SECTOR_SIZE];
    uint8_t replacement[BLOCK_CHANNEL_SECTOR_SIZE];
    uint8_t reread[BLOCK_CHANNEL_SECTOR_SIZE];
    memset(replacement, 0xa5, sizeof(replacement));

    TEST_ASSERT_EQUAL(1,
                      unwrap_result(BlockCache__read(&cache, &client, 4, 1, (Address)original, sizeof(original)), 0));
    fail_writes = true;
    match_result(BlockCache__write(&cache, &client, 4, 1, (Address)replacement, sizeof(replacement)),
                 success_test_fail(),
        error(code, message)
        {
        }
    );
    TEST_ASSERT_EQUAL(1, unwrap_result(BlockCache__read(&cache, &client, 4, 1, (Address)reread, sizeof(reread)), 0));
    TEST_ASSERT_EQUAL_MEMORY(original, reread, sizeof(original));
    TEST_ASSERT_EQUAL_UINT64(1, read_calls);
}

void test_block_cache_failed_read_is_not_cached(void)
{
    BlockCache cache = BlockCache__new();
    BlockClient client = {0};
    uint8_t data[BLOCK_CHANNEL_SECTOR_SIZE];

    fail_reads = true;
    match_result(BlockCache__read(&cache, &client, 4, 1, (Address)data, sizeof(data)),
        success_test_fail(),
                 error(code, message)
        {
        }
    );
    fail_reads = false;
    TEST_ASSERT_EQUAL(1, unwrap_result(BlockCache__read(&cache, &client, 4, 1, (Address)data, sizeof(data)), 0));
    TEST_ASSERT_EQUAL_UINT64(2, read_calls);
}
