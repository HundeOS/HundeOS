#include "unity.h"
#include "../../../kernel/heap.h"
#include "../../../kernel/system_info.h"
#include "../../../kernel/types/result.h"

Heap heap = NEW_HEAP;

#define TEST_HEAP_PAGES 3000
#define MANY_LIVE_ALLOCATIONS 1100

static uint8_t test_heap_memory[PAGE_SIZE * TEST_HEAP_PAGES];
static KernelAddress many_live_allocations[MANY_LIVE_ALLOCATIONS];
static const Address test_heap_physical_base = 0x70000000ULL;

static void add_free_region(SystemInfoPtr system_info, Address physical_start, Address physical_end)
{
    PhysicalMemoryRegion region = PhysicalMemoryRegion__new(physical_start, physical_end);
    match_result(PhysicalMemoryRegions__push(&system_info->physical_memory.free_regions, region),
        success_do_nothing(),
                 error_test_fail()
    );
}

static SystemInfo test_system_info(void)
{
    SystemInfo system_info = SystemInfo__new();
    system_info.kernel_heap.virtual_start = (Address)&test_heap_memory[0];
    system_info.kernel_heap.physical_start = test_heap_physical_base;
    system_info.kernel_heap.physical_end = test_heap_physical_base + sizeof(test_heap_memory);
    return system_info;
}

static void assert_free_success(KernelAddress addr)
{
    match_result(Heap__free(&heap, addr),
        success_do_nothing(),
        error_test_fail()
    );
}

static void assert_free_fails(KernelAddress addr)
{
    match_result(Heap__free(&heap, addr),
        success_test_fail(),
        error_test_ok()
    );
}

void setUp(void)
{
    SystemInfo system_info = test_system_info();
    match_result(Heap__init(&heap, &system_info),
        success_do_nothing(),
        error_test_fail()
    );
}

static void init_heap_with_coarse_block_size(size_t coarse_block_size)
{
    SystemInfo system_info = test_system_info();
    system_info.heap_coarse_block_size = coarse_block_size;
    match_result(Heap__init(&heap, &system_info),
        success_do_nothing(),
        error_test_fail()
    );
}

static void init_heap_with_two_regions(uint64_t first_pages, uint64_t gap_pages, uint64_t second_pages)
{
    SystemInfo system_info = test_system_info();
    Address second_start = test_heap_physical_base + ((first_pages + gap_pages) * PAGE_SIZE);
    add_free_region(&system_info, test_heap_physical_base, test_heap_physical_base + (first_pages * PAGE_SIZE));
    add_free_region(&system_info, second_start, second_start + (second_pages * PAGE_SIZE));
    match_result(Heap__init(&heap, &system_info),
        success_do_nothing(),
        error_test_fail()
    );
}

void tearDown(void)
{
}

void test_simple_heap_alloc_and_free(void)
{
    KernelAddress addr = Heap__alloc_size(&heap, 1024);
    Address physical = paddr(addr);

    assert_free_success(addr);

    KernelAddress second_alloc = Heap__alloc_size(&heap, 1024);
    Address physical_second_alloc = paddr(second_alloc);

    // check the memory has been recycled
    TEST_ASSERT_EQUAL(physical, physical_second_alloc);
}

void test_heap_memory_state_tracks_allocated_and_freed_pages(void)
{
    HeapMemoryState initial = Heap__memory_state(&heap);
    KernelAddress allocation = Heap__alloc_size(&heap, PAGE_SIZE * 3U);
    HeapMemoryState allocated = Heap__memory_state(&heap);

    TEST_ASSERT_EQUAL_UINT64(TEST_HEAP_PAGES, initial.total_pages);
    TEST_ASSERT_EQUAL_UINT64(TEST_HEAP_PAGES, initial.free_pages);
    TEST_ASSERT_EQUAL_UINT64(initial.free_pages - 3U, allocated.free_pages);

    assert_free_success(allocation);
    HeapMemoryState released = Heap__memory_state(&heap);
    TEST_ASSERT_EQUAL_UINT64(initial.free_pages, released.free_pages);
}

void test_reclaimable_allocation_preserves_half_of_heap(void)
{
    KernelAddress reclaimable = Heap__try_alloc_reclaimable_size(&heap, PAGE_SIZE * (TEST_HEAP_PAGES / 2U));
    TEST_ASSERT_TRUE(reclaimable.valid);

    KernelAddress refused = Heap__try_alloc_reclaimable_size(&heap, PAGE_SIZE);
    TEST_ASSERT_FALSE(refused.valid);
    TEST_ASSERT_EQUAL_UINT64(TEST_HEAP_PAGES / 2U, Heap__memory_state(&heap).free_pages);
}

void test_heap_pressure_generation_changes_only_at_watermarks(void)
{
    HeapMemoryState initial = Heap__memory_state(&heap);
    KernelAddress normal = Heap__alloc_size(&heap, PAGE_SIZE * (TEST_HEAP_PAGES * 3U / 4U));
    HeapMemoryState after_normal = Heap__memory_state(&heap);
    TEST_ASSERT_EQUAL(HEAP_PRESSURE_NORMAL, after_normal.pressure_level);
    TEST_ASSERT_EQUAL_UINT64(initial.pressure_generation, after_normal.pressure_generation);

    KernelAddress low = Heap__alloc_size(&heap, PAGE_SIZE * (TEST_HEAP_PAGES / 10U));
    HeapMemoryState after_low = Heap__memory_state(&heap);
    TEST_ASSERT_EQUAL(HEAP_PRESSURE_LOW, after_low.pressure_level);
    TEST_ASSERT_EQUAL_UINT64(initial.pressure_generation + 1U, after_low.pressure_generation);

    KernelAddress critical = Heap__alloc_size(&heap, PAGE_SIZE * (TEST_HEAP_PAGES / 20U));
    HeapMemoryState after_critical = Heap__memory_state(&heap);
    TEST_ASSERT_EQUAL(HEAP_PRESSURE_CRITICAL, after_critical.pressure_level);
    TEST_ASSERT_EQUAL_UINT64(initial.pressure_generation + 2U, after_critical.pressure_generation);

    assert_free_success(critical);
    assert_free_success(low);
    assert_free_success(normal);
    HeapMemoryState released = Heap__memory_state(&heap);
    TEST_ASSERT_EQUAL(HEAP_PRESSURE_NORMAL, released.pressure_level);
}

void test_heap_init_uses_system_info_physical_base(void)
{
    KernelAddress addr = Heap__alloc(&heap);

    TEST_ASSERT_EQUAL_UINT64((Address)&test_heap_memory[0], vaddr(addr));
    TEST_ASSERT_EQUAL_UINT64(test_heap_physical_base, paddr(addr));
}

void test_heap_alloc_reuses_freed_single_page(void)
{
    KernelAddress addr = Heap__alloc(&heap);
    Address physical = paddr(addr);

    assert_free_success(addr);

    KernelAddress second_alloc = Heap__alloc(&heap);

    TEST_ASSERT_EQUAL(physical, paddr(second_alloc));
}

void test_smaller_alloc_from_larger_free_block_splits_remainder(void)
{
    KernelAddress addr = Heap__alloc_size(&heap, PAGE_SIZE * 4);

    assert_free_success(addr);

    KernelAddress first_page = Heap__alloc_size(&heap, PAGE_SIZE);
    KernelAddress remainder = Heap__alloc_size(&heap, PAGE_SIZE * 3);

    TEST_ASSERT_EQUAL(vaddr(addr), vaddr(first_page));
    TEST_ASSERT_EQUAL(paddr(addr), paddr(first_page));
    TEST_ASSERT_EQUAL(vaddr(addr) + PAGE_SIZE, vaddr(remainder));
    TEST_ASSERT_EQUAL(paddr(addr) + PAGE_SIZE, paddr(remainder));
}

void test_adjacent_free_pages_coalesce_for_larger_alloc(void)
{
    KernelAddress first = Heap__alloc_size(&heap, PAGE_SIZE);
    KernelAddress second = Heap__alloc_size(&heap, PAGE_SIZE);

    assert_free_success(first);
    assert_free_success(second);

    KernelAddress combined = Heap__alloc_size(&heap, PAGE_SIZE * 2);

    TEST_ASSERT_EQUAL(vaddr(first), vaddr(combined));
    TEST_ASSERT_EQUAL(paddr(first), paddr(combined));
}

void test_double_free_returns_error(void)
{
    KernelAddress addr = Heap__alloc_size(&heap, PAGE_SIZE);

    assert_free_success(addr);
    assert_free_fails(addr);
}

void test_freeing_inside_allocated_block_returns_error(void)
{
    KernelAddress addr = Heap__alloc_size(&heap, PAGE_SIZE * 2);
    KernelAddress interior = KERNEL_ADDR(vaddr(addr) + PAGE_SIZE, paddr(addr) + PAGE_SIZE);

    assert_free_fails(interior);
}

void test_reused_multi_page_alloc_is_zeroed(void)
{
    KernelAddress addr = Heap__alloc_size(&heap, PAGE_SIZE * 2);
    uint8_t * bytes = (uint8_t *)vaddr(addr);

    bytes[0] = 0xAA;
    bytes[PAGE_SIZE] = 0xBB;

    assert_free_success(addr);

    KernelAddress reused = Heap__alloc_size(&heap, PAGE_SIZE * 2);
    uint8_t * reused_bytes = (uint8_t *)vaddr(reused);

    TEST_ASSERT_EQUAL(vaddr(addr), vaddr(reused));
    TEST_ASSERT_EQUAL(0, reused_bytes[0]);
    TEST_ASSERT_EQUAL(0, reused_bytes[PAGE_SIZE]);
}

void test_cross_coarse_block_allocation_free_reuses_whole_range(void)
{
    init_heap_with_coarse_block_size(PAGE_SIZE * 3);
    size_t cross_block_size = PAGE_SIZE * 5;

    KernelAddress addr = Heap__alloc_size(&heap, cross_block_size);
    Address physical = paddr(addr);

    assert_free_success(addr);

    KernelAddress reused = Heap__alloc_size(&heap, cross_block_size);

    TEST_ASSERT_EQUAL(vaddr(addr), vaddr(reused));
    TEST_ASSERT_EQUAL(physical, paddr(reused));
}

void test_stack_sized_allocation_crossing_five_coarse_blocks_frees_whole_range(void)
{
    init_heap_with_coarse_block_size(_2MB);

    KernelAddress addr = Heap__alloc_size(&heap, STACK_SIZE);
    Address physical = paddr(addr);

    assert_free_success(addr);

    KernelAddress smaller = Heap__alloc_size(&heap, _2MB);
    TEST_ASSERT_EQUAL(vaddr(addr), vaddr(smaller));
    TEST_ASSERT_EQUAL(physical, paddr(smaller));

    KernelAddress reused = Heap__alloc_size(&heap, STACK_SIZE - _2MB);

    TEST_ASSERT_EQUAL(vaddr(addr) + _2MB, vaddr(reused));
    TEST_ASSERT_EQUAL(physical + _2MB, paddr(reused));
}

void test_heap_tracks_more_than_one_old_bucket_of_live_allocations(void)
{
    for (uint64_t i = 0; i < MANY_LIVE_ALLOCATIONS; i++)
    {
        many_live_allocations[i] = Heap__alloc(&heap);
        TEST_ASSERT_TRUE(many_live_allocations[i].valid);
    }

    for (uint64_t i = 0; i < MANY_LIVE_ALLOCATIONS; i++)
    {
        assert_free_success(many_live_allocations[i]);
    }
}

void test_heap_hashes_page_addresses_by_page_number(void)
{
    Address first = 0xfffff00017201000;
    Address second = first + PAGE_SIZE;

    TEST_ASSERT_EQUAL_UINT64(Heap__hash_address(&first) + 1, Heap__hash_address(&second));
}

void test_heap_allocates_from_discontiguous_system_info_regions(void)
{
    init_heap_with_two_regions(2, 3, 3);

    KernelAddress first = Heap__alloc_size(&heap, 2 * PAGE_SIZE);
    KernelAddress second = Heap__alloc_size(&heap, 3 * PAGE_SIZE);

    TEST_ASSERT_EQUAL_UINT64(test_heap_physical_base, paddr(first));
    TEST_ASSERT_EQUAL_UINT64((Address)&test_heap_memory[0], vaddr(first));
    TEST_ASSERT_EQUAL_UINT64(test_heap_physical_base + (5 * PAGE_SIZE), paddr(second));
    TEST_ASSERT_EQUAL_UINT64((Address)&test_heap_memory[5 * PAGE_SIZE], vaddr(second));
}

void test_heap_does_not_join_capacity_across_reserved_hole(void)
{
    init_heap_with_two_regions(2, 2, 2);

    KernelAddress crossing = Heap__try_alloc_size(&heap, 3 * PAGE_SIZE);

    TEST_ASSERT_FALSE(crossing.valid);
    TEST_ASSERT_TRUE(Heap__try_alloc_size(&heap, 2 * PAGE_SIZE).valid);
    TEST_ASSERT_TRUE(Heap__try_alloc_size(&heap, 2 * PAGE_SIZE).valid);
}

void test_freed_regions_do_not_coalesce_across_reserved_hole(void)
{
    init_heap_with_two_regions(2, 2, 2);
    KernelAddress first = Heap__alloc_size(&heap, 2 * PAGE_SIZE);
    KernelAddress second = Heap__alloc_size(&heap, 2 * PAGE_SIZE);

    assert_free_success(first);
    assert_free_success(second);

    TEST_ASSERT_FALSE(Heap__try_alloc_size(&heap, 4 * PAGE_SIZE).valid);
    TEST_ASSERT_TRUE(Heap__try_alloc_size(&heap, 2 * PAGE_SIZE).valid);
    TEST_ASSERT_TRUE(Heap__try_alloc_size(&heap, 2 * PAGE_SIZE).valid);
}

void test_adjacent_system_info_regions_form_one_contiguous_extent(void)
{
    init_heap_with_two_regions(2, 0, 2);

    KernelAddress allocation = Heap__try_alloc_size(&heap, 4 * PAGE_SIZE);

    TEST_ASSERT_TRUE(allocation.valid);
    TEST_ASSERT_EQUAL_UINT64(test_heap_physical_base, paddr(allocation));
}

void test_heap_clips_system_info_regions_to_mapped_heap(void)
{
    SystemInfo system_info = test_system_info();
    add_free_region(&system_info, test_heap_physical_base - PAGE_SIZE, test_heap_physical_base + PAGE_SIZE);
    add_free_region(&system_info, system_info.kernel_heap.physical_end,
                    system_info.kernel_heap.physical_end + PAGE_SIZE);
    match_result(Heap__init(&heap, &system_info),
        success_do_nothing(),
        error_test_fail()
    );

    KernelAddress allocation = Heap__try_alloc_size(&heap, PAGE_SIZE);

    TEST_ASSERT_TRUE(allocation.valid);
    TEST_ASSERT_EQUAL_UINT64(test_heap_physical_base, paddr(allocation));
    TEST_ASSERT_FALSE(Heap__try_alloc_size(&heap, PAGE_SIZE).valid);
}

void test_aligned_allocation_splits_prefix_and_preserves_physical_alignment(void)
{
    KernelAddress prefix = Heap__alloc(&heap);
    KernelAddress aligned = Heap__try_alloc_size_aligned(&heap, _2MB + PAGE_SIZE, _2MB);

    TEST_ASSERT_TRUE(aligned.valid);
    TEST_ASSERT_EQUAL_UINT64(0, paddr(aligned) % _2MB);
    TEST_ASSERT_EQUAL_UINT64(test_heap_physical_base + _2MB, paddr(aligned));

    assert_free_success(prefix);
    assert_free_success(aligned);
    KernelAddress coalesced = Heap__try_alloc_size(&heap, sizeof(test_heap_memory));
    TEST_ASSERT_TRUE(coalesced.valid);
}

void test_aligned_allocation_rejects_non_page_alignment(void)
{
    TEST_ASSERT_FALSE(Heap__try_alloc_size_aligned(&heap, PAGE_SIZE, PAGE_SIZE / 2).valid);
}
