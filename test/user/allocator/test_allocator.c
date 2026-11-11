#include "unity.h"
#include <stdlib.h>
#include "sys/user_mutex.h"

#define malloc ProbeAllocator__malloc
#define free ProbeAllocator__free
#define calloc ProbeAllocator__calloc
#define realloc ProbeAllocator__realloc
#define malloc_usable_size ProbeAllocator__usable_size
#define memalign ProbeAllocator__memalign
#include "../../../user/lib/newlib/malloc.c"
#undef malloc
#undef free
#undef calloc
#undef realloc
#undef malloc_usable_size
#undef memalign

static bool locked;
static unsigned locks;
static unsigned unlocks;
static size_t recorded_size;
static size_t recorded_count;
static void * recorded_allocation;
static uintptr_t storage;

ThreadWaitStatus UserMutex__lock(UserMutexPtr self)
{
    TEST_ASSERT_EQUAL_PTR(&allocator_mutex, self);
    TEST_ASSERT_FALSE(locked);
    locked = true;
    locks++;
    return THREAD_WAIT_WOKEN;
}

ThreadWaitStatus UserMutex__unlock(UserMutexPtr self)
{
    TEST_ASSERT_EQUAL_PTR(&allocator_mutex, self);
    TEST_ASSERT_TRUE(locked);
    locked = false;
    unlocks++;
    return THREAD_WAIT_WOKEN;
}

void * Allocator__malloc_unlocked(size_t size)
{
    TEST_ASSERT_TRUE(locked);
    recorded_size = size;
    return &storage;
}

void Allocator__free_unlocked(void * allocation)
{
    TEST_ASSERT_TRUE(locked);
    recorded_allocation = allocation;
}

void * Allocator__calloc_unlocked(size_t count, size_t size)
{
    TEST_ASSERT_TRUE(locked);
    recorded_count = count;
    recorded_size = size;
    return &storage;
}

void * Allocator__realloc_unlocked(void * allocation, size_t size)
{
    TEST_ASSERT_TRUE(locked);
    recorded_allocation = allocation;
    recorded_size = size;
    return &storage;
}

size_t Allocator__usable_size_unlocked(void * allocation)
{
    TEST_ASSERT_TRUE(locked);
    recorded_allocation = allocation;
    return 512;
}

void * Allocator__memalign_unlocked(size_t alignment, size_t size)
{
    TEST_ASSERT_TRUE(locked);
    recorded_count = alignment;
    recorded_size = size;
    return &storage;
}

void setUp(void)
{
    locked = false;
    locks = 0;
    unlocks = 0;
    recorded_size = 0;
    recorded_count = 0;
    recorded_allocation = NULL;
}

void tearDown(void)
{
    TEST_ASSERT_FALSE(locked);
    TEST_ASSERT_EQUAL_UINT(1, locks);
    TEST_ASSERT_EQUAL_UINT(1, unlocks);
}

void test_malloc_holds_lock_through_core_call(void)
{
    TEST_ASSERT_EQUAL_PTR(&storage, ProbeAllocator__malloc(123));
    TEST_ASSERT_EQUAL_UINT(123, recorded_size);
}

void test_free_holds_lock_through_core_call(void)
{
    ProbeAllocator__free(&storage);
    TEST_ASSERT_EQUAL_PTR(&storage, recorded_allocation);
}

void test_calloc_holds_one_lock_for_nested_allocation(void)
{
    TEST_ASSERT_EQUAL_PTR(&storage, ProbeAllocator__calloc(7, 31));
    TEST_ASSERT_EQUAL_UINT(7, recorded_count);
    TEST_ASSERT_EQUAL_UINT(31, recorded_size);
}

void test_realloc_holds_one_lock_for_compound_operation(void)
{
    TEST_ASSERT_EQUAL_PTR(&storage, ProbeAllocator__realloc(&storage, 91));
    TEST_ASSERT_EQUAL_PTR(&storage, recorded_allocation);
    TEST_ASSERT_EQUAL_UINT(91, recorded_size);
}

void test_usable_size_holds_lock_while_reading_metadata(void)
{
    TEST_ASSERT_EQUAL_UINT(512, ProbeAllocator__usable_size(&storage));
    TEST_ASSERT_EQUAL_PTR(&storage, recorded_allocation);
}

void test_memalign_holds_one_lock_for_compound_operation(void)
{
    TEST_ASSERT_EQUAL_PTR(&storage, ProbeAllocator__memalign(256, 123));
    TEST_ASSERT_EQUAL_UINT(256, recorded_count);
    TEST_ASSERT_EQUAL_UINT(123, recorded_size);
}
