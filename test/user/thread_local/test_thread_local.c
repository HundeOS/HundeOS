#include "unity.h"
#include "sys/thread_local.h"
#include "sys/thread_local_arch.h"
#include "sys/mem.h"
#include "sys/syscall.h"

#include <stdlib.h>
#include <string.h>

static ThreadLocalImage configured_image;
static bool allocation_fails;
static unsigned allocation_count;
static unsigned release_count;
static unsigned query_count;

size_t LibC__thread_state_size(void)
{
    return 32;
}

Result(Anything) mem_alloc_aligned_pages_result(size_t size, size_t alignment)
{
    if (allocation_fails)
    {
        return Error(Anything, bstr("Allocation failure"), MEM_ALLOCATION_FAILED);
    }
    void * allocation = aligned_alloc(alignment, (size + alignment - 1) & ~(alignment - 1));
    TEST_ASSERT_NOT_NULL(allocation);
    allocation_count++;
    return Success(Anything, (Anything)allocation);
}

MemStatus mem_free_pages(void * allocation)
{
    release_count++;
    free(allocation);
    return MEM_OK;
}

SystemCallResult syscall(SystemCall call, Anything first, Anything second, Anything third, Anything fourth,
                         Anything fifth)
{
    TEST_ASSERT_EQUAL(SYSCALL_THREAD_LOCAL_IMAGE, call);
    *(ThreadLocalImagePtr)first = configured_image;
    query_count++;
    return THREAD_OPERATION_OK;
}

void setUp(void)
{
    ThreadLocalArch__set_pointer(0);
    configured_image = ThreadLocalImage__new(0, 0, 0, 1);
    allocation_fails = false;
    allocation_count = 0;
    release_count = 0;
    query_count = 0;
}

void tearDown(void)
{
    TEST_ASSERT_EQUAL(THREAD_OPERATION_OK, ThreadLocal__destroy_current());
}

void test_initialization_is_idempotent_and_provides_zeroed_libc_state(void)
{
    TEST_ASSERT_EQUAL(THREAD_OPERATION_OK, ThreadLocal__initialize());
    Address pointer = ThreadLocalArch__pointer();
    Address state = ThreadLocal__libc_state();
    TEST_ASSERT_NOT_EQUAL(0, pointer);
    TEST_ASSERT_NOT_EQUAL(0, state);
    uint8_t zeroes[32] = {0};
    TEST_ASSERT_EQUAL_MEMORY(zeroes, (void *)state, sizeof(zeroes));
    TEST_ASSERT_EQUAL(THREAD_OPERATION_OK, ThreadLocal__initialize());
    TEST_ASSERT_EQUAL_UINT64(pointer, ThreadLocalArch__pointer());
    TEST_ASSERT_EQUAL_UINT(1, allocation_count);
    TEST_ASSERT_EQUAL_UINT(1, query_count);
}

void test_created_thread_copies_template_instead_of_current_tls_values(void)
{
    uint64_t initializer[] = {41, 73};
    configured_image = ThreadLocalImage__new((Address)initializer, sizeof(initializer), 64, 64);
    TEST_ASSERT_EQUAL(THREAD_OPERATION_OK, ThreadLocal__initialize());
    Address first_pointer = ThreadLocalArch__pointer();
    Address first_libc = ThreadLocal__libc_state();
    uint64_t * first_image = (uint64_t *)(first_pointer + 64);
    TEST_ASSERT_EQUAL_HEX64(41, first_image[0]);
    TEST_ASSERT_EQUAL_HEX64(73, first_image[1]);
    TEST_ASSERT_EQUAL_HEX64(0, first_image[2]);
    first_image[0] = 999;
    *(uint64_t *)first_libc = 55;
    match_result(
        ThreadLocal__new_thread(),
        success(block)
        {
            TEST_ASSERT_NOT_EQUAL(first_pointer, block.thread_pointer);
            TEST_ASSERT_EQUAL_UINT64(0, block.thread_pointer % 64);
            uint64_t * second_image = (uint64_t *)(block.thread_pointer + 64);
            TEST_ASSERT_EQUAL_HEX64(41, second_image[0]);
            TEST_ASSERT_EQUAL_HEX64(0, second_image[2]);
            ThreadLocalArch__set_pointer(block.thread_pointer);
            TEST_ASSERT_NOT_EQUAL(first_libc, ThreadLocal__libc_state());
            TEST_ASSERT_EQUAL_HEX64(0, *(uint64_t *)ThreadLocal__libc_state());
            TEST_ASSERT_EQUAL(THREAD_OPERATION_OK, ThreadLocal__destroy_current());
            ThreadLocalArch__set_pointer(first_pointer);
            TEST_ASSERT_EQUAL_HEX64(999, first_image[0]);
            TEST_ASSERT_EQUAL_HEX64(55, *(uint64_t *)first_libc);
        },
        error_test_fail()
    );
}

void test_destroy_clears_thread_pointer_and_releases_storage_once(void)
{
    TEST_ASSERT_EQUAL(THREAD_OPERATION_OK, ThreadLocal__initialize());
    TEST_ASSERT_EQUAL(THREAD_OPERATION_OK, ThreadLocal__destroy_current());
    TEST_ASSERT_EQUAL_UINT64(0, ThreadLocalArch__pointer());
    TEST_ASSERT_EQUAL_UINT64(0, ThreadLocal__libc_state());
    TEST_ASSERT_EQUAL_UINT(1, release_count);
    TEST_ASSERT_EQUAL(THREAD_OPERATION_OK, ThreadLocal__destroy_current());
    TEST_ASSERT_EQUAL_UINT(1, release_count);
}

void test_invalid_template_does_not_allocate_storage(void)
{
    ThreadLocalImage invalid = ThreadLocalImage__new(1, 9, 8, 3);
    match_result(
        ThreadLocal__new(&invalid),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(THREAD_OPERATION_INVALID, code);
        }
    );
    TEST_ASSERT_EQUAL_UINT(0, allocation_count);
}

void test_allocation_failure_does_not_install_thread_pointer(void)
{
    allocation_fails = true;
    TEST_ASSERT_EQUAL(THREAD_OPERATION_NO_MEMORY, ThreadLocal__initialize());
    TEST_ASSERT_EQUAL_UINT64(0, ThreadLocalArch__pointer());
}
