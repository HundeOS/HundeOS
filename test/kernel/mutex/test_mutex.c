#include <stdint.h>
#include "unity.h"
#include "mutex.h"
#include "types/result.h"
#include "address.h"

static unsigned lock_count;
static unsigned unlock_count;
static unsigned evaluation_count;
static unsigned protected_value;
static mutex_t lock;

CPUID CPU__id(void)
{
    return 0;
}

void setUp(void)
{
    lock = MUTEX_INIT;
    lock_count = 0;
    unlock_count = 0;
    evaluation_count = 0;
    protected_value = 42;
}

void tearDown(void)
{
}

void mutex_lock(MutexPtr self)
{
    TEST_ASSERT_EQUAL_UINT32(0, self->v);
    self->v = 1;
    lock_count++;
}

int mutex_trylock(MutexPtr self)
{
    if (self->v != 0)
    {
        return 0;
    }
    mutex_lock(self);
    return 1;
}

void mutex_unlock(MutexPtr self)
{
    TEST_ASSERT_EQUAL_UINT32(1, self->v);
    self->v = 0;
    protected_value = 99;
    unlock_count++;
}

static unsigned read_protected_value(void)
{
    TEST_ASSERT_EQUAL_UINT32(1, lock.v);
    evaluation_count++;
    return protected_value;
}

static unsigned return_scalar(void)
{
    critical_section(
        lock,
        {
            cs_return(read_protected_value());
        }
    );

    return 0;
}

void test_return_evaluates_once_before_unlock_and_captures_value(void)
{
    TEST_ASSERT_EQUAL_UINT(42, return_scalar());
    TEST_ASSERT_EQUAL_UINT(1, evaluation_count);
    TEST_ASSERT_EQUAL_UINT(1, lock_count);
    TEST_ASSERT_EQUAL_UINT(1, unlock_count);
    TEST_ASSERT_EQUAL_UINT32(0, lock.v);
}

static Result(Address) return_result(bool failed)
{
    critical_section(
        lock,
        {
            if (failed)
            {
                cs_return(Error(Address, bstr("Expected error"), -2));
            }
            cs_return(Success(Address, read_protected_value()));
        }
    );
    return Error(Address, bstr("Unknown error"), -1);
}

void test_return_preserves_structured_success_and_error_values(void)
{
    match_result(return_result(false),
        success(value)
        {
            TEST_ASSERT_EQUAL_UINT(42, value);
        },
        error_test_fail()
    );
    TEST_ASSERT_EQUAL_UINT(1, unlock_count);
    match_result(
        return_result(true),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(-2, code);
            TEST_ASSERT_EQUAL_BSTRING(bstr("Expected error"), message);
        }
    );
    TEST_ASSERT_EQUAL_UINT(2, unlock_count);
}

static void return_void(void)
{
    critical_section(
        lock,
        {
            cs_return_void();
        }
    );
}

void test_void_return_unlocks_once(void)
{
    return_void();
    TEST_ASSERT_EQUAL_UINT(1, unlock_count);
    TEST_ASSERT_EQUAL_UINT32(0, lock.v);
}

static unsigned try_return(void)
{
    critical_section_try(
        lock,
        {
            cs_return(read_protected_value());
        },
        {
        }
    );

    return 0;
}

void test_try_return_evaluates_before_unlock(void)
{
    TEST_ASSERT_EQUAL_UINT(42, try_return());
    TEST_ASSERT_EQUAL_UINT(1, evaluation_count);
    TEST_ASSERT_EQUAL_UINT(1, unlock_count);
}

static void exit_after_unlock(void)
{
    critical_section(
        lock,
        {
            cs_exit_then(
                {
                    TEST_ASSERT_EQUAL_UINT32(0, lock.v);
                    TEST_ASSERT_EQUAL_UINT(99, protected_value);
                    return;
                });
        }
    );
}

void test_exit_then_unlocks_before_running_exit_code(void)
{
    exit_after_unlock();
    TEST_ASSERT_EQUAL_UINT(1, unlock_count);
    TEST_ASSERT_EQUAL_UINT32(0, lock.v);
}
