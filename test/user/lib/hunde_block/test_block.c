#include "../../../hundetest.h"
#include "unity.h"
#include "../../../../user/lib/hunde/native.h"
#include "../../../../user/lib/channel/block_client.h"
#include "../../../../user/lib/channel/block_cache.h"
#include "../../../../user/lib/sys/mem.h"
#include <stdlib.h>
#include <string.h>

static void * client;
static bool open_failure;
static bool transfer_failure;
static bool short_transfer;
static ChannelCode close_code;
static uint64_t opens;
static uint64_t transfers;
static uint64_t closes;
static bool wrote;
static BString opened_path;
static BlockProtocolBlockNumber observed_sector;
static BlockProtocolBlockCount observed_count;
static uint8_t buffer[1024];
static uint8_t disk[2048][512];
static uint64_t allocations;
static uint64_t frees;

Result(Anything) mem_alloc_reclaimable_pages_result(size_t size)
{
    allocations++;
    return Success(Anything, (Anything)calloc(1, size));
}

MemStatus mem_free_pages(void * address)
{
    frees++;
    free(address);
    return MEM_OK;
}

Result(BlockClient) BlockClient__open(ConstBStringPtr path)
{
    opens++;
    opened_path = *path;
    if (open_failure)
    {
        return Error(BlockClient, bstr("Device unavailable"), CHANNEL_UNAVAILABLE);
    }
    return Success(BlockClient, ((BlockClient){.block_count = 2048, .open = true}));
}

ChannelCode BlockClient__close(BlockClientPtr self)
{
    closes++;
    self->open = false;
    return close_code;
}

Result(BlockProtocolBlockCount) BlockClient__read(BlockClientPtr self, BlockProtocolBlockNumber sector,
                                                  BlockProtocolBlockCount count, Address destination, size_t size)
{
    TEST_ASSERT_EQUAL_UINT64(count * 512, size);
    transfers++;
    observed_sector = sector;
    observed_count = count;
    if (transfer_failure)
    {
        return Error(BlockProtocolBlockCount, bstr("Device failed"), BLOCK_PROTOCOL_IO_ERROR);
    }
    memcpy((void *)destination, disk[sector], size);
    return Success(BlockProtocolBlockCount, short_transfer ? count - 1 : count);
}

Result(BlockProtocolBlockCount) BlockClient__write(BlockClientPtr self, BlockProtocolBlockNumber sector,
                                                   BlockProtocolBlockCount count, Address source, size_t size)
{
    wrote = true;
    transfers++;
    observed_sector = sector;
    observed_count = count;
    TEST_ASSERT_EQUAL_UINT64(count * 512, size);
    if (transfer_failure)
    {
        return Error(BlockProtocolBlockCount, bstr("Device failed"), BLOCK_PROTOCOL_IO_ERROR);
    }
    if (!transfer_failure && !short_transfer)
    {
        memcpy(disk[sector], (const void *)source, size);
    }
    return Success(BlockProtocolBlockCount, short_transfer ? count - 1 : count);
}

void setUp(void)
{
    client = calloc(1, hunde_rust_block_layout().size);
    TEST_ASSERT_NOT_NULL(client);
    memset(disk, 0, sizeof(disk));
    memset(buffer, 0, sizeof(buffer));
    allocations = frees = 0;
    open_failure = false;
    transfer_failure = false;
    short_transfer = false;
    close_code = CHANNEL_OK;
    opens = transfers = closes = 0;
    wrote = false;
}

void tearDown(void)
{
    hunde_rust_block_close(client);
    free(client);
}

void test_native_block_layout_and_open_preserve_device_geometry(void)
{
    RustNativeLayout layout = hunde_rust_block_layout();
    TEST_ASSERT_GREATER_THAN_UINT64(sizeof(BlockClient) + sizeof(BlockCache) - 1, layout.size);
    TEST_ASSERT_EQUAL_UINT64(_Alignof(BlockClient), layout.alignment);
    RustBlockOpenResult result = hunde_rust_block_open(client, HundeByteSpan__new((const uint8_t *)"disk0", 5));
    TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, result.kind);
    TEST_ASSERT_EQUAL_UINT64(2048, result.sector_count);
    TEST_ASSERT_EQUAL_BSTRING(bstr("disk0"), opened_path);
}

void test_native_block_open_rejects_bad_paths_and_preserves_errors(void)
{
    TEST_ASSERT_EQUAL(HUNDE_ABI_ERROR, hunde_rust_block_open(client, HundeByteSpan__new(buffer, 0)).kind);
    TEST_ASSERT_EQUAL(HUNDE_ABI_ERROR, hunde_rust_block_open(client, HundeByteSpan__new(buffer, 1024)).kind);
    TEST_ASSERT_EQUAL(HUNDE_ABI_ERROR, hunde_rust_block_open(client, HundeByteSpan__new(buffer, 3)).kind);
    TEST_ASSERT_EQUAL_UINT64(0, opens);
    open_failure = true;
    RustBlockOpenResult result = hunde_rust_block_open(client, HundeByteSpan__new((const uint8_t *)"disk0", 5));
    TEST_ASSERT_EQUAL(HUNDE_ABI_ERROR, result.kind);
    TEST_ASSERT_EQUAL(CHANNEL_UNAVAILABLE, result.error.code);
}

void test_native_block_transfers_use_sector_counts_and_reject_short_completions(void)
{
    TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, hunde_rust_block_read(client, 7, buffer, sizeof(buffer)).kind);
    TEST_ASSERT_EQUAL_UINT64(7, observed_sector);
    TEST_ASSERT_EQUAL_UINT64(2, observed_count);
    TEST_ASSERT_FALSE(wrote);
    TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, hunde_rust_block_write(client, 8, buffer, sizeof(buffer)).kind);
    TEST_ASSERT_TRUE(wrote);
    short_transfer = true;
    HundeStatusResult result = hunde_rust_block_read(client, 10, buffer, sizeof(buffer));
    TEST_ASSERT_EQUAL(HUNDE_ABI_ERROR, result.kind);
    TEST_ASSERT_EQUAL(BLOCK_PROTOCOL_IO_ERROR, result.error.code);
    transfer_failure = true;
    result = hunde_rust_block_write(client, 7, buffer, sizeof(buffer));
    TEST_ASSERT_EQUAL(HUNDE_ABI_ERROR, result.kind);
    TEST_ASSERT_EQUAL(BLOCK_PROTOCOL_IO_ERROR, result.error.code);
}

void test_native_block_rejects_invalid_lengths_before_transport(void)
{
    TEST_ASSERT_EQUAL(HUNDE_ABI_ERROR, hunde_rust_block_read(client, 0, buffer, 0).kind);
    TEST_ASSERT_EQUAL(HUNDE_ABI_ERROR, hunde_rust_block_write(client, 0, buffer, 513).kind);
    TEST_ASSERT_EQUAL_UINT64(0, transfers);
}

void test_native_block_close_propagates_cleanup_failure(void)
{
    TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, hunde_rust_block_close(client).kind);
    close_code = CHANNEL_UNAVAILABLE;
    HundeStatusResult result = hunde_rust_block_close(client);
    TEST_ASSERT_EQUAL(HUNDE_ABI_ERROR, result.kind);
    TEST_ASSERT_EQUAL(CHANNEL_UNAVAILABLE, result.error.code);
    TEST_ASSERT_EQUAL_UINT64(2, closes);
}

void test_native_block_cache_reuses_reads_and_keeps_writes_and_rollback_coherent(void)
{
    memset(disk[7], 42, sizeof(buffer));
    TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, hunde_rust_block_read(client, 7, buffer, sizeof(buffer)).kind);
    TEST_ASSERT_EQUAL_UINT64(1, transfers);
    memset(buffer, 0, sizeof(buffer));
    TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, hunde_rust_block_read(client, 7, buffer, sizeof(buffer)).kind);
    TEST_ASSERT_EQUAL_UINT64(1, transfers);
    TEST_ASSERT_EACH_EQUAL_UINT8(42, buffer, sizeof(buffer));
    memset(buffer, 99, sizeof(buffer));
    TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, hunde_rust_block_write(client, 7, buffer, sizeof(buffer)).kind);
    memset(buffer, 0, sizeof(buffer));
    TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, hunde_rust_block_read(client, 7, buffer, sizeof(buffer)).kind);
    TEST_ASSERT_EQUAL_UINT64(2, transfers);
    TEST_ASSERT_EACH_EQUAL_UINT8(99, buffer, sizeof(buffer));
    transfer_failure = true;
    memset(buffer, 17, sizeof(buffer));
    TEST_ASSERT_EQUAL(HUNDE_ABI_ERROR, hunde_rust_block_write(client, 7, buffer, sizeof(buffer)).kind);
    transfer_failure = false;
    TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, hunde_rust_block_read(client, 7, buffer, sizeof(buffer)).kind);
    TEST_ASSERT_EACH_EQUAL_UINT8(99, buffer, sizeof(buffer));
    memset(buffer, 42, sizeof(buffer));
    TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, hunde_rust_block_write(client, 7, buffer, sizeof(buffer)).kind);
    memset(buffer, 0, sizeof(buffer));
    TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, hunde_rust_block_read(client, 7, buffer, sizeof(buffer)).kind);
    TEST_ASSERT_EACH_EQUAL_UINT8(42, buffer, sizeof(buffer));
    TEST_ASSERT_EQUAL_MEMORY(disk[7], buffer, sizeof(buffer));
}

void test_native_block_close_reclaims_cache_growth(void)
{
    for (uint64_t sector = 0; sector <= BLOCK_CACHE_CAPACITY; sector++)
    {
        TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, hunde_rust_block_read(client, sector, buffer, 512).kind);
    }
    TEST_ASSERT_EQUAL_UINT64(1, allocations);
    TEST_ASSERT_EQUAL_UINT64(0, frees);
    TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, hunde_rust_block_close(client).kind);
    TEST_ASSERT_EQUAL_UINT64(1, frees);
}

void test_native_block_reclaim_frees_growth_without_closing_the_client(void)
{
    for (uint64_t sector = 0; sector <= BLOCK_CACHE_CAPACITY; sector++)
    {
        TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, hunde_rust_block_read(client, sector, buffer, 512).kind);
    }
    TEST_ASSERT_GREATER_THAN_UINT64(0, allocations);
    uint64_t transfer_count = transfers;
    hunde_rust_block_reclaim(client);
    TEST_ASSERT_EQUAL_UINT64(allocations, frees);
    TEST_ASSERT_EQUAL_UINT64(0, closes);
    TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, hunde_rust_block_read(client, BLOCK_CACHE_CAPACITY, buffer, 512).kind);
    TEST_ASSERT_EQUAL_UINT64(transfer_count + 1, transfers);
    hunde_rust_block_reclaim(client);
    TEST_ASSERT_EQUAL_UINT64(allocations, frees);
}
