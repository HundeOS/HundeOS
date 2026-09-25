#include "unity.h"
#include "../../../kernel/memory_accounting.h"

static MemoryAccounting accounting;

void setUp(void)
{
    accounting = MemoryAccounting__new();
}

void tearDown(void)
{
    MemoryAccounting__forget(&accounting, 1);
    MemoryAccounting__forget(&accounting, 2);
    MemoryAccounting__forget(&accounting, 3);
}

static void map(Address space, Address start, Address backing, size_t size, MemoryMappingKind kind)
{
    MemoryMapping mapping = MemoryMapping__new(space, start, backing, size, kind);
    MemoryAccounting__map(&accounting, &mapping);
}

static void expect_usage(Address space, uint64_t private_bytes, uint64_t shared_bytes, uint64_t device_bytes)
{
    match_optional(
        MemoryAccounting__usage(&accounting, space),
        some(usage)
        {
            TEST_ASSERT_EQUAL_UINT64(private_bytes, usage.private_bytes);
            TEST_ASSERT_EQUAL_UINT64(shared_bytes, usage.shared_bytes);
            TEST_ASSERT_EQUAL_UINT64(device_bytes, usage.device_bytes);
        },
        none_test_fail()
    );
}

void test_private_shared_and_device_mappings_are_separate(void)
{
    map(1, 0x1000, 0x10000, 12288, MEMORY_PRIVATE);
    map(1, 0x5000, 0x80000, 4096, MEMORY_SHARED);
    map(1, 0x9000, 0x90000, 4096, MEMORY_DEVICE);
    expect_usage(1, 12288, 4096, 4096);
    expect_usage(2, 0, 0, 0);
}

void test_exported_subranges_move_from_private_to_shared_once_for_all_peers(void)
{
    map(1, 0x1000, 0x10000, 12288, MEMORY_PRIVATE);
    map(2, 0x5000, 0x11000, 4096, MEMORY_SHARED);
    map(3, 0x5000, 0x11000, 8192, MEMORY_SHARED);
    expect_usage(1, 4096, 8192, 0);
    expect_usage(2, 0, 4096, 0);
    expect_usage(3, 0, 8192, 0);
    MemoryAccounting__forget(&accounting, 3);
    expect_usage(1, 8192, 4096, 0);
    MemoryAccounting__forget(&accounting, 2);
    expect_usage(1, 12288, 0, 0);
}

void test_partial_unmap_preserves_prefix_and_suffix_backing(void)
{
    map(1, 0x1000, 0x10000, 12288, MEMORY_PRIVATE);
    MemoryAccounting__unmap(&accounting, 1, 0x2000, 4096);
    map(2, 0x1000, 0x12000, 4096, MEMORY_SHARED);
    expect_usage(1, 4096, 4096, 0);
    MemoryAccounting__unmap(&accounting, 1, 0x2000, 4096);
    expect_usage(1, 4096, 4096, 0);
}

void test_remap_replaces_accounting_and_disposal_removes_all_regions(void)
{
    map(1, 0x1000, 0x10000, 12288, MEMORY_PRIVATE);
    map(1, 0x2000, 0x50000, 4096, MEMORY_DEVICE);
    expect_usage(1, 8192, 0, 4096);
    map(1, 0x2000, 0x50000, 4096, MEMORY_DEVICE);
    expect_usage(1, 8192, 0, 4096);
    MemoryAccounting__forget(&accounting, 1);
    expect_usage(1, 0, 0, 0);
}
