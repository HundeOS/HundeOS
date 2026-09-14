#include <pthread.h>
#include <string.h>
#include "unity.h"
#include "../../../common/types/atomic_record_array.h"

typedef struct TestRecord
{
    uint64_t value;
    uint64_t complement;
} TestRecord;
ResultDef(TestRecord);
AtomicRecordArrayDefine(TestRecord, 4, TestRecords);
static TestRecords records;
static unsigned completed;

void setUp(void)
{
    memset(&records, 0, sizeof(records));
    TestRecord record = {.value = 0, .complement = UINT64_MAX};
    TestRecords__set(&records, 0, &record);
    completed = 0;
}
void tearDown(void)
{
}

static void * writer(void * context)
{
    for (uint64_t value = 1; value <= 100000; value++)
    {
        TestRecord record = {.value = value, .complement = ~value};
        TestRecords__set(&records, 0, &record);
    }
    __atomic_store_n(&completed, 1, __ATOMIC_RELEASE);
    return context;
}

void test_atomic_record_array_never_returns_a_torn_record(void)
{
    pthread_t thread;
    TEST_ASSERT_EQUAL(0, pthread_create(&thread, NULL, writer, &records));
    bool torn = false;
    do
    {
        match_result(
            TestRecords__at(&records, 0),
            success(value) { torn |= value.complement != ~value.value; },
            error(code, message) { torn |= code != ATOMIC_RECORD_BUSY; }
        );
    } while (!__atomic_load_n(&completed, __ATOMIC_ACQUIRE));
    TEST_ASSERT_EQUAL(0, pthread_join(thread, NULL));
    TEST_ASSERT_FALSE(torn);
    match_result(
        TestRecords__at(&records, 0),
        success(value) { TEST_ASSERT_EQUAL_UINT64(100000, value.value); },
        error_test_fail()
    );
}

void test_atomic_record_array_validates_indices(void)
{
    TestRecord value = {0};
    match_result(
        TestRecords__set(&records, 4, &value),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(ATOMIC_RECORD_INVALID, code); }
    );
    match_result(
        TestRecords__at(&records, 4),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(ATOMIC_RECORD_INVALID, code); }
    );
}
