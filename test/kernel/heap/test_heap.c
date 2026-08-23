#include "unity.h"
#include "../../../kernel/heap.h"
#include "../../../kernel/system_info.h"
#include "../../../kernel/types/result.h"

Heap heap = NEW_HEAP;

static uint8_t test_heap_memory[PAGE_SIZE * 16];
static const Address test_heap_physical_base = 0x70000000ULL;

static void assert_free_success(KernelAddress addr)
{
    match_result(HeapCode, Heap__free(&heap, addr), success(_) {}, error(code, message) { TEST_FAIL(); })
}

static void assert_free_fails(KernelAddress addr)
{
    match_result(HeapCode, Heap__free(&heap, addr), success(code) { TEST_FAIL(); }, error(code, message) {})
}

void setUp(void)
{
    SystemInfo system_info = {
        .kernel_heap = {
            .virtual_start = (Address)&test_heap_memory[0],
            .physical_start = test_heap_physical_base,
            .physical_end = test_heap_physical_base + sizeof(test_heap_memory),
        },
    };
    match_result(HeapCode, Heap__init(&heap, &system_info), success(_) {}, error(code, message) { TEST_FAIL(); })
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
    uint8_t * bytes = (uint8_t *) vaddr(addr);

    bytes[0] = 0xAA;
    bytes[PAGE_SIZE] = 0xBB;

    assert_free_success(addr);

    KernelAddress reused = Heap__alloc_size(&heap, PAGE_SIZE * 2);
    uint8_t * reused_bytes = (uint8_t *) vaddr(reused);

    TEST_ASSERT_EQUAL(vaddr(addr), vaddr(reused));
    TEST_ASSERT_EQUAL(0, reused_bytes[0]);
    TEST_ASSERT_EQUAL(0, reused_bytes[PAGE_SIZE]);
}
