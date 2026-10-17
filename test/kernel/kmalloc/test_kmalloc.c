#include <pthread.h>
#include <stdint.h>

#include "unity.h"

#include "../../../kernel/kmalloc.h"

#define THREAD_COUNT 4
#define THREAD_ALLOCATIONS 256

typedef struct AllocationThread
{
    size_t size;
    bool failed;
} AllocationThread;

typedef AllocationThread * AllocationThreadPtr;

static void assert_free_success(KernelAddress allocation)
{
    match_result(Kernel__free_malloc(allocation.virtual_addr),
        success_do_nothing(),
        error_test_fail()
    );
}

void setUp(void)
{
}

void tearDown(void)
{
}

void test_small_allocations_share_a_backing_page(void)
{
    KernelAddress first = Kernel__malloc(72);
    KernelAddress second = Kernel__malloc(72);

    TEST_ASSERT_TRUE(first.valid);
    TEST_ASSERT_TRUE(second.valid);
    TEST_ASSERT_EQUAL_UINT64(0, first.virtual_addr % 16U);
    TEST_ASSERT_EQUAL_UINT64(0, second.virtual_addr % 16U);
    TEST_ASSERT_EQUAL_UINT64(first.virtual_addr & ~(PAGE_SIZE - 1U), second.virtual_addr & ~(PAGE_SIZE - 1U));

    assert_free_success(first);
    assert_free_success(second);
}

void test_freed_small_allocation_is_reused(void)
{
    KernelAddress first = Kernel__malloc(40);
    assert_free_success(first);

    KernelAddress reused = Kernel__malloc(40);

    TEST_ASSERT_EQUAL_UINT64(first.virtual_addr, reused.virtual_addr);
    assert_free_success(reused);
}

void test_large_allocation_uses_malloc_header_and_frees(void)
{
    KernelAddress allocation = Kernel__malloc(PAGE_SIZE);

    TEST_ASSERT_TRUE(allocation.valid);
    *(uint64_t *)allocation.virtual_addr = 0x123456789abcdef0ULL;
    TEST_ASSERT_EQUAL_UINT64(0x123456789abcdef0ULL, *(uint64_t *)allocation.virtual_addr);
    assert_free_success(allocation);
}

static void * allocate_repeatedly(void * context)
{
    AllocationThreadPtr thread = context;
    for (size_t iteration = 0; iteration < THREAD_ALLOCATIONS; iteration++)
    {
        KernelAddress allocation = Kernel__malloc(thread->size);
        if (!allocation.valid)
        {
            thread->failed = true;
            return context;
        }
        *(uint64_t *)allocation.virtual_addr = iteration;
        match_result(
            Kernel__free_malloc(allocation.virtual_addr),
            success_do_nothing(),
            error(code, message)
            {
                thread->failed = true;
                return context;
            }
        );
    }
    return context;
}

void test_small_allocator_is_thread_safe(void)
{
    pthread_t threads[THREAD_COUNT];
    pthread_attr_t attributes;
    AllocationThread allocations[THREAD_COUNT] = {
        {.size = 8},
        {.size = 40},
        {.size = 72},
        {.size = 200},
    };
    TEST_ASSERT_EQUAL(0, pthread_attr_init(&attributes));
    for (size_t thread = 0; thread < THREAD_COUNT; thread++)
    {
        TEST_ASSERT_EQUAL(0, pthread_create(&threads[thread], &attributes, allocate_repeatedly, &allocations[thread]));
    }
    for (size_t thread = 0; thread < THREAD_COUNT; thread++)
    {
        void * result;
        TEST_ASSERT_EQUAL(0, pthread_join(threads[thread], &result));
        TEST_ASSERT_EQUAL_PTR(&allocations[thread], result);
        TEST_ASSERT_FALSE(allocations[thread].failed);
    }
    TEST_ASSERT_EQUAL(0, pthread_attr_destroy(&attributes));
}
