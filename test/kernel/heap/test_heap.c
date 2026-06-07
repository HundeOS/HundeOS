#include "unity.h"
#include "../../../kernel/heap.h"
#include "../../../kernel/types/result.h"

Heap heap = NEW_HEAP;

static void assert_free_success(KernelAddress addr)
{
    ResultMatch(HeapCode, Heap__free(&heap, addr))
        IsError(code, message, TEST_FAIL());
    End
}

static void assert_free_fails(KernelAddress addr)
{
    ResultMatch(HeapCode, Heap__free(&heap, addr))
        IsSuccess(code, TEST_FAIL());
    End
}

void setUp(void)
{
    ResultMatch(HeapCode, Heap__init(&heap))
        IsError(code, message, TEST_FAIL());
    End
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
